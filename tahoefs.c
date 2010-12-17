/*
 * Copyright 2010 IIJ Innovation Institute Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY IIJ INNOVATION INSTITUTE INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL IIJ INNOVATION INSTITUTE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <math.h>
#include <errno.h>
#include <assert.h>
#include <err.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "tahoefs.h"
#include "http_stub.h"
#include "json_stub.h"
#include "filecache.h"

#define TAHOE_DEFAULT_DIR ".tahoe"
#define TAHOE_DEFAULT_ALIASES_PATH "private/aliases"
#define TAHOE_DEFAULT_ROOT_ALIAS "tahoe:"

#define TAHOE_DEFAULT_WEBAPI_SERVER "localhost"
#define TAHOE_DEFAULT_WEBAPI_PORT "3456"

#define TAHOE_DEFAULT_FILECACHE_DIR ".tahoefs"

tahoefs_global_config_t config;

static int tahoe_getattr(const char *, struct stat *);
static int tahoe_open(const char *, struct fuse_file_info *);
static int tahoe_read(const char *, char *, size_t, off_t,
		      struct fuse_file_info *);
static int tahoe_readdir(const char *, void *, fuse_fill_dir_t, off_t,
			 struct fuse_file_info *);
static int tahoe_readdir_callback(tahoefs_readdir_baton_t *);
static void *tahoe_init(struct fuse_conn_info *);
static void tahoe_destroy(void *);
const char *tahoe_default_root_cap(void);

static struct fuse_operations tahoe_oper = {
  .init		= tahoe_init,
  .destroy	= tahoe_destroy,
  .getattr	= tahoe_getattr,
  .open		= tahoe_open,
  .read		= tahoe_read,
  .readdir	= tahoe_readdir,
};

#define TAHOEFS_OPT(t, p) { t, offsetof(struct tahoefs_global_config, p), 1 }
static const struct fuse_opt tahoefs_opts[] = {
  TAHOEFS_OPT("-t %s",		tahoe_dir),
  TAHOEFS_OPT("--tahoe-dir-%s",	tahoe_dir),
  TAHOEFS_OPT("-r %s",		root_cap),
  TAHOEFS_OPT("--root-cap-%s",	root_cap),
  TAHOEFS_OPT("-s %s",		webapi_server),
  TAHOEFS_OPT("--server=%s",	webapi_server),
  TAHOEFS_OPT("-p %s",		webapi_port),
  TAHOEFS_OPT("--port=%s",	webapi_port),
  TAHOEFS_OPT("-c %s",		filecache_dir),
  TAHOEFS_OPT("--cache-dir=%s",	filecache_dir),
  FUSE_OPT_END
};

static int tahoe_getattr(const char *path, struct stat *stbufp)
{
  char *infop = NULL;
  size_t info_size;
  if (http_stub_get_info(path, &infop, &info_size) == -1) {
    warnx("failed to get node information of %s.", path);
    return (-ENOENT);
  }

  /* convert the JSON data to tahoefs metadata. */
  tahoefs_stat_t tstat;
  memset(&tstat, 0, sizeof(tahoefs_stat_t));
  if (json_stub_json_to_metadata(infop, &tstat) == -1) {
    warnx("failed to convert JSON data to tahoefs stat structure.");
    return (-ENOENT);
  }

  /* free the memory which keeps the HTTP response body. */
  free(infop);

  /* fill the struct stat{} structure. */
  switch (tstat.type) {
  case TAHOEFS_METADATA_TYPE_DIRNODE:
    stbufp->st_mode = S_IFDIR | 0700;
    /* XXX we have no idea about the directory size. */
    stbufp->st_size = 4096;
    break;
  case TAHOEFS_METADATA_TYPE_FILENODE:
    stbufp->st_mode = S_IFREG | 0600;
    stbufp->st_size = tstat.size;
    break;
  default:
    warnx("unknown tahoefs node type %d.", tstat.type);
    return (-ENOENT);
  }

  /* # of hard links. */
  stbufp->st_nlink = 1;

  /* uid and gid. */
  stbufp->st_uid = getuid();
  stbufp->st_gid = getgid();

  /* # of 512B blocks allocated.  does it make any sense to set it? */
  stbufp->st_blocks = 0;

  /* timestamps. */
  stbufp->st_ctime = stbufp->st_atime = stbufp->st_mtime
    = tstat.link_modification_time;

  return (0);
}

static int tahoe_open(const char *path, struct fuse_file_info *fi)
{
  if (fi->flags & (O_WRONLY|O_RDWR|O_APPEND|O_CREAT|O_TRUNC)) {
    return (-EPERM);
  }

  struct stat stbuf;
  memset(&stbuf, 0, sizeof(struct stat));
  if (tahoe_getattr(path, &stbuf) == -1) {
    /* cannot get attribute of the file. */
    return (-ENOENT);
  }

  return (0);
}

static int tahoe_read(const char *path, char *buf, size_t size,
		      off_t offset, struct fuse_file_info *fi)
{
  int read = filecache_read(path, buf, size, offset);
  if (read == -1) {
    warnx("read %ld bytes at %ld from %s failed.", size, offset, path);
    return (-1);
  }

  return (read);
}

static int
tahoe_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
	      off_t offset, struct fuse_file_info *fi)
{
  char *infop = NULL;
  size_t info_size;
  if (http_stub_get_info(path, &infop, &info_size) == -1) {
    warnx("failed to get dirnode information of %s.", path);
    return (-ENOENT);
  }

  if (json_stub_iterate_children(buf, filler, infop,
				 tahoe_readdir_callback) == -1) {
    warnx("failed to iterate child nodes of %s.", path);
    free(infop);
    return (-1);
  }

  /* free the memory which keeps the HTTP response body. */
  free(infop);

  return (0);
}

static int
tahoe_readdir_callback(tahoefs_readdir_baton_t *batonp)
{
  assert(batonp != NULL);

#if 0  
  /* convert the JSON data to tahoefs metadata. */
  tahoefs_metadata_t metadata;
  memset(&metadata, 0, sizeof (tahoefs_metadata_t));
  if (json_stub_json_to_metadata(batonp->infop, &metadata) == -1) {
    warnx("failed to convert JSON data to tahoefs metadata.");
    return (-1);
  }
  /* XXX convert metadata to struct stat. */
#endif

  fuse_fill_dir_t filler = (fuse_fill_dir_t)batonp->fillerp;
  if (filler(batonp->nodename_listp, batonp->nodename,
		     NULL /* XXX struct stat */, 0) == 1) {
    warnx("failed to fill directory list buffer.");
    return (-1);
  };

  return (0);
}

static void *
tahoe_init(struct fuse_conn_info *conn)
{
  if (http_stub_initialize() == -1) {
    errx(EXIT_FAILURE, "failed to initialize the http_stub module.");
  }

  return (NULL);
}

static void
tahoe_destroy(void *dummy)
{
  if (http_stub_terminate() == -1) {
    errx(EXIT_FAILURE, "failed to teminate the http_stub module.");
  }
}

const char *
tahoe_default_root_cap(void)
{
  assert(config.tahoe_dir != NULL);

  char alias_path[MAXPATHLEN];
  alias_path[0] = '\0';
  if (config.tahoe_dir[0] != '/') {
    strcat(alias_path, getenv("HOME"));
    strcat(alias_path, "/");
  }
  strcat(alias_path, config.tahoe_dir);
  strcat(alias_path, "/");
  strcat(alias_path, TAHOE_DEFAULT_ALIASES_PATH);
  FILE *fp;
  fp = fopen(alias_path, "r");
  if (fp == NULL) {
    /*
     * no .tahoe/private/aliases file found. -r option must be exist
     * in this case.
     */
    return (NULL);
  }

  char alias[256]; /* XXX dirty. */
  while (!feof(fp)) {
    if (fgets(alias, sizeof(alias), fp) == NULL) {
      warn("failed to read alias definition.");
      fclose(fp);
      return (NULL);
    }

   if (strstr(alias, TAHOE_DEFAULT_ROOT_ALIAS) != alias) {
      /* this is not a "tahoe:" line. */
      continue;
    }

    char *uri = strstr(alias, "URI:");
    if (uri == NULL) {
      warn("unexpected alias format %s.", alias);
      continue;
    }

    char *newline = strrchr(uri, '\n');
    if (newline) {
      *newline = '\0';
    }

    fclose(fp);
    return (strdup(uri));
  }

  fclose(fp);
  return (NULL);
}

int main(int argc, char *argv[])
{
  memset(&config, 0, sizeof(tahoefs_global_config_t));
  config.tahoe_dir = TAHOE_DEFAULT_DIR;
  config.webapi_server = TAHOE_DEFAULT_WEBAPI_SERVER;
  config.webapi_port = TAHOE_DEFAULT_WEBAPI_PORT;
  config.filecache_dir = TAHOE_DEFAULT_FILECACHE_DIR;

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  if (fuse_opt_parse(&args, &config, tahoefs_opts, NULL) == -1) {
    errx(EXIT_FAILURE, "failed to parse options.");
  }
  if (config.root_cap == NULL) {
    config.root_cap = tahoe_default_root_cap();
    if (config.root_cap == NULL) {
      err(EXIT_FAILURE, "failed to get your ROOT_CAP information.");
    }
  }

  return fuse_main(args.argc, args.argv, &tahoe_oper, NULL);
}
