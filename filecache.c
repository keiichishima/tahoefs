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
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <assert.h>
#include <err.h>

#include "tahoefs.h"
#include "http_stub.h"

#define FILECACHE_CONTENTS_DIRNAME "contents"
#define FILECACHE_METADATA_DIRNAME "metadata"

#define FILECACHE_PATH_TO_CACHE_PATH(path, cache_path) { \
    (cache_path)[0] = '\0';				 \
    if (config.filecache_dir[0] != '/') {		 \
      strcat((cache_path), getenv("HOME"));		 \
      strcat((cache_path), "/");			 \
    }							 \
    strcat((cache_path), config.filecache_dir);		 \
    strcat((cache_path), "/");				 \
    strcat((cache_path), FILECACHE_CONTENTS_DIRNAME);	 \
    strcat((cache_path), (path));			 \
}

static int filecache_check_file(const char *);
static int filecache_cache_file(const char *, const char *);
static int filecache_mkdir_parent(const char *);

int
filecache_getattr(const char *path, tahoefs_stat_t *tstatp)
{
  assert(path != NULL);
  assert(tstatp != NULL);

  return (-1);
}

int
filecache_read(const char *path, char *buf, size_t size, off_t offset)
{
  assert(path != NULL);
  assert(buf != NULL);

  char cache_path[MAXPATHLEN];
  FILECACHE_PATH_TO_CACHE_PATH(path, cache_path)

  if (filecache_check_file(cache_path) == -1) {
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
filecache_check_file(const char *cache_path)
{
  assert(cache_path != NULL);

  struct stat stbuf;
  if (stat(cache_path, &stbuf) == -1) {
    warn("failed to stat a cache file for %s.", cache_path);
    return (-1);
  }
  return (0);
}

static int
filecache_cache_file(const char *path, const char *cache_path)
{

  if (filecache_mkdir_parent(cache_path) == -1) {
    warnx("failed to create a parent directory of %s.", cache_path);
    return (-1);
  }

  if (http_stub_read_file(path, cache_path) == -1) {
    warnx("failed to cache the contents of the file %s.", path);
    return (-1);
  }
  
  return (0);
}

static int
filecache_mkdir_parent(const char *path)
{
  assert(path != NULL);

  char *parent = strdup(path);
  if (parent == NULL) {
    warn("failed to allocate memory in duplicating string %s.", path);
    return (-1);
  }
  char *slash = strrchr(parent, '/');
  if (slash == NULL) {
    warnx("invalid cache_path value %s.", path);
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
