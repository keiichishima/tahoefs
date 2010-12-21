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

#ifdef __linux__
#define _XOPEN_SOURCE 500
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <assert.h>
#include <err.h>

#include "tahoefs.h"
#include "http_stub.h"
#include "json_stub.h"

#define FILECACHE_PATH_TO_CACHE_PATH(path, cache_path) { \
    (cache_path)[0] = '\0';				    \
    if (config.filecache_dir[0] != '/') {		    \
      strcat((cache_path), getenv("HOME"));		    \
      strcat((cache_path), "/");			    \
    }							    \
    strcat((cache_path), config.filecache_dir);		    \
    strcat((cache_path), (path));			    \
}

#define FILECACHE_INFO_ATTR "user.net.iijlab.tahoefs.info"

static ssize_t filecache_get_info_attr(const char *, void **);
static int filecache_set_info_attr(const char *, void *, size_t);
static int filecache_check_file(const char *, struct stat *);
static int filecache_cache_file(const char *, const char *);
static int filecache_uncache_file(const char *);
static int filecache_mkdir_parent(const char *);

int
filecache_getattr(const char *path, tahoefs_stat_t *tstatp)
{
  assert(path != NULL);
  assert(tstatp != NULL);

  char *remote_infop = NULL;
  size_t remote_info_size;
  char cached_path[MAXPATHLEN];
  FILECACHE_PATH_TO_CACHE_PATH(path, cached_path);
  if (http_stub_get_info(path, &remote_infop, &remote_info_size) == -1) {
    /*
     * tahoe storage doesn't have the specified file or directory.
     * the local cache entry and children (if it is a directory) must
     * be removed here.
     */
    if (filecache_uncache_file(cached_path) == -1) {
      warnx("failed to remove a cache for %s.", cached_path);
    }
    return (-1);
  }

  /* convert the infop (in JSON) to tahoefs_stat_t{} structure. */
  if (json_stub_jsonstring_to_tstat(remote_infop, tstatp) == -1) {
    warnx("failed to convert JSON data to tahoefs stat structure.");
    free(remote_infop);
    return (-1);
  }
  free(remote_infop);

  int need_update = 0;
  tahoefs_stat_t cached_tstat;
  memset(&cached_tstat, 0, sizeof(tahoefs_stat_t));
  struct stat cached_stat;
  memset(&cached_stat, 0, sizeof(struct stat));
  if (filecache_check_file(cached_path, &cached_stat) == 0) {
    /* cache exists. check if we need to update it or not. */

    /* read the cached metadata stored as JSON string. */
    ssize_t cached_info_size;
    char *cached_infop = NULL;
    cached_info_size = filecache_get_info_attr(cached_path,
					       (void **)&cached_infop);
    if (cached_info_size == -1) {
#ifdef DEBUG
      printf("cache %s exists but without any attribute.  probably it is a directory.\n",
	     cached_path);
#endif
      if (tstatp->type != TAHOEFS_STAT_TYPE_DIRNODE) {
	if (filecache_uncache_file(cached_path) == -1) {
	  warnx("failed to remove a cache for %s.", cached_path);
	  return (-1);
	}
      }
      return (0);
    }

    /* convert the info to tahoefs_stat_t{} structure. */
    if (json_stub_jsonstring_to_tstat(cached_infop, &cached_tstat) == -1) {
      warn("failed to convert cached JSON string at %s to tahoefs_stat_t.",
	   cached_path);
      free(cached_infop);
      return (-1);
    }
    free(cached_infop);

    /* compare if the cached one and remote one are identical. */
    if (tstatp->type != cached_tstat.type) {
      need_update = 1;
    } else if (strcmp(tstatp->verify_uri, cached_tstat.verify_uri) != 0) {
      need_update = 1;
    } else if (tstatp->link_creation_time
	       != cached_tstat.link_creation_time) {
      need_update = 1;
    } else if (tstatp->link_modification_time
	       > cached_tstat.link_modification_time) {
      need_update = 1;
    }
  }

  if (need_update) {
    if (filecache_uncache_file(cached_path) == -1) {
      warnx("failed to remove a cache for %s.", cached_path);
      return (-1);
    }
  }

  return (0);
}

static ssize_t
filecache_get_info_attr(const char *cached_path, void **infopp)
{
  assert(cached_path != NULL);
  assert(infopp != NULL);
  assert(*infopp == NULL);

  ssize_t info_size;
  info_size = getxattr(cached_path, FILECACHE_INFO_ATTR, NULL, 0,
#if defined(__APPLE__)
		       0,
#endif
		       0);
  if (info_size == -1) {
    warn("failed to retreive the size of tahoefs_info attr of %s.",
	 cached_path);
    return (-1);
  }

  char *infop = malloc(info_size);
  info_size = getxattr(cached_path, FILECACHE_INFO_ATTR, infop, info_size,
#if defined(__APPLE__)
		       0,
#endif
		       0);
  if (info_size == -1) {
    warn("failed to retreive the value of tahoefs_info attr of %s.",
	 cached_path);
    free(infop);
    return (-1);
  }

  *infopp = infop;
  return (info_size);
}

static int
filecache_set_info_attr(const char *cached_path, void *infop, size_t info_size)
{
  assert(cached_path != NULL);
  assert(infop != NULL);

  if (setxattr(cached_path, FILECACHE_INFO_ATTR, infop, info_size,
#if defined(__APPLE__)
	       0,
#endif
	       0) == -1) {
    warn("failed to set tahoefs_info attr to %s.", cached_path);
    return (-1);
  }
  return (0);
}

int
filecache_read(const char *path, char *buf, size_t size, off_t offset)
{
  assert(path != NULL);
  assert(buf != NULL);

  char cache_path[MAXPATHLEN];
  FILECACHE_PATH_TO_CACHE_PATH(path, cache_path);

  struct stat stat;
  memset(&stat, 0, sizeof(struct stat));
  if (filecache_check_file(cache_path, &stat) == -1) {
    filecache_cache_file(path, cache_path);
  }

  int fd = open(cache_path, O_RDONLY);
  if (fd == -1) {
    warn("failed to open cache file %s.", cache_path);
    return (-1);
  }
  ssize_t read = pread(fd, buf, size, offset);
  close(fd);

  return (read);
}

static int
filecache_check_file(const char *cached_path, struct stat *statp)
{
  assert(cached_path != NULL);
  assert(statp != NULL);

  if (stat(cached_path, statp) == -1) {
    warn("failed to stat a cache file for %s.", cached_path);
    return (-1);
  }
  return (0);
}

static int
filecache_cache_file(const char *path, const char *cached_path)
{

  if (filecache_mkdir_parent(cached_path) == -1) {
    warnx("failed to create a parent directory of %s.", cached_path);
    return (-1);
  }

  if (http_stub_read_file(path, cached_path) == -1) {
    warnx("failed to cache the contents of the file %s.", path);
    return (-1);
  }

  char *cached_infop = NULL;
  size_t cached_info_size;
  if (http_stub_get_info(path, &cached_infop, &cached_info_size) == -1) {
    warnx("failed to get nodeinfo of the file %s.", path);
    return (-1);
  }
  if (filecache_set_info_attr(cached_path, cached_infop, cached_info_size)
      == -1) {
    warnx("failed to set xattr of tahoefs_info attr to %s.", cached_path);
    free(cached_infop);
    unlink(cached_path);
    return (-1);
  }
  free(cached_infop);
  
  return (0);
}

static int
filecache_uncache_file(const char *cached_path)
{
  struct stat stbuf;
  memset(&stbuf, 0, sizeof(struct stat));
  if (stat(cached_path, &stbuf) == -1) {
    warn("failed to get stat{} of %s.", cached_path);
    return (-1);
  }

  if (stbuf.st_mode & S_IFDIR) {
    /* is a directory. */
    char child_path[MAXPATHLEN];
    DIR *dirp;
    dirp = opendir(cached_path);
    struct dirent *dentp;
    while ((dentp = readdir(dirp)) != NULL) {
      if (strcmp(dentp->d_name, ".") == 0)
	continue;
      if (strcmp(dentp->d_name, "..") == 0)
	continue;

      child_path[0] = '\0';
      strcat(child_path, cached_path);
      strcat(child_path, "/");
      strcat(child_path, dentp->d_name);
      struct stat child_stbuf;
      memset(&child_stbuf, 0, sizeof(struct stat));
      if (stat(child_path, &child_stbuf) == -1) {
	warn("failed to get stat{} of child %s.", child_path);
	return (-1);
      }
      if (child_stbuf.st_mode & S_IFDIR) {
	if (filecache_uncache_file(child_path) == -1) {
	  warn("failed to unlink child %s recursively.", child_path);
	  return (-1);
	}
      } else {
	if (unlink(child_path) == -1) {
	  warn("failed to unlink child %s.", child_path);
	  return (-1);
	}
      }
    }
    if (rmdir(cached_path) == -1) {
      warn("failed to rmkdir %s.", cached_path);
      return (-1);
    }
  } else {
    /* is a file. */
    if (unlink(cached_path) == -1) {
      warn("failed to unlink %s.", cached_path);
      return (-1);
    }
  }
  return (0);
}

static int
filecache_mkdir_parent(const char *cached_path)
{
  assert(cached_path != NULL);

  char *parent = strdup(cached_path);
  if (parent == NULL) {
    warn("failed to allocate memory in duplicating string %s.", cached_path);
    return (-1);
  }
  char *slash = strrchr(parent, '/');
  if (slash == NULL) {
    warnx("invalid cache_path value %s.", cached_path);
    free(parent);
    return (-1);
  }
  *slash = '\0';

  struct stat stbuf;
  if (stat(parent, &stbuf) == -1) {
    if (filecache_mkdir_parent(parent) == -1) {
      warnx("failed to create a parent directory %s.", parent);
      free(parent);
      return (-1);
    }
    free(parent);

    if (mkdir(parent, S_IRWXU) == -1) {
      warn("failed to create a directory %s.", parent);
      return (-1);
    }
  }
  return (0);
}
