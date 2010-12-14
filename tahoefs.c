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
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <math.h>
#include <errno.h>
#include <assert.h>
#include <err.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "tahoefs.h"
#include "http_stub.h"
#include "json_stub.h"

static int tahoe_getattr(const char *path, struct stat *stbufp)
{
  char *infop = NULL;
  size_t info_size;
  if (http_stub_get_info(path, &infop, &info_size) == -1) {
    warnx("failed to get node information of %s.", path);
    return (-ENOENT);
  }

  /* convert the JSON data to tahoefs metadata. */
  struct tahoefs_metadata metadata;
  memset(&metadata, 0, sizeof (struct tahoefs_metadata));
  if (json_stub_json_to_metadata(infop, &metadata) == -1) {
    warnx("failed to convert JSON data to tahoefs metadata.");
    return (-ENOENT);
  }

  /* free the memory which keeps the HTTP response body. */
  free(infop);

  switch (metadata.type) {
  case TAHOEFS_METADATA_TYPE_DIRNODE:
    stbufp->st_mode = S_IFDIR | 0700;
    /* XXX we have no idea about the directory size. */
    stbufp->st_size = 4096;
    break;
  case TAHOEFS_METADATA_TYPE_FILENODE:
    stbufp->st_mode = S_IFREG | 0600;
    stbufp->st_size = metadata.size;
    break;
  default:
    warnx("unknown tahoefs node type %d.");
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
    = metadata.link_modification_time;

  return (0);
}

static int tahoe_open(const char *path, struct fuse_file_info *fi)
{
  (void) fi;

  if(strcmp(path, "/") != 0)
    return -ENOENT;

  return 0;
}

static int tahoe_read(const char *path, char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;

	if(strcmp(path, "/") != 0)
		return -ENOENT;

	if (offset >= (1ULL << 32))
		return 0;

	return size;
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

  return (-ENOENT);
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

static struct fuse_operations tahoe_oper = {
  .init		= tahoe_init,
  .destroy	= tahoe_destroy,
  .getattr	= tahoe_getattr,
  .open		= tahoe_open,
  .read		= tahoe_read,
  .readdir	= tahoe_readdir,
};

int main(int argc, char *argv[])
{

  return fuse_main(argc, argv, &tahoe_oper, NULL);
}
