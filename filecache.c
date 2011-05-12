/*
 * Copyright 2010, 2011 IIJ Innovation Institute Inc. All rights reserved.
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
#include <errno.h>
#include <assert.h>
#include <err.h>

#include "tahoefs.h"
#include "http_stub.h"
#include "json_stub.h"

#define FILECACHE_SUPPORTED_OPEN_FLAGS (O_RDONLY|O_WRONLY|O_RDWR|O_CREAT|O_TRUNC)
#define FILECACHE_PATH_TO_CACHED_PATH(path, cached_path) do {	    \
    (cached_path)[0] = '\0';					    \
    if (config.filecache_dir[0] != '/') {			    \
      strcat((cached_path), getenv("HOME"));			    \
      strcat((cached_path), "/");				    \
    }								    \
    strcat((cached_path), config.filecache_dir);		    \
    strcat((cached_path), (path));				    \
  } while (0);

#define FILECACHE_INFO_ATTR "user.net.iijlab.tahoefs.info"
#define FILECACHE_HAS_CONTENTS "user.net.iijlab.tahoefs.has_contents"

static int filecache_getattr_from_parent(const char *, tahoefs_stat_t *);
static int filecache_cached_getattr(const char *, tahoefs_stat_t *);
static ssize_t filecache_get_info_xattr(const char *, void **);
static int filecache_set_info_xattr(const char *, void *, size_t);
static int filecache_get_cache_stat(const char *, struct stat *);
static int filecache_cache_file(const char *, const char *);
static int filecache_cache_directory(const char *, const char *, char *, int);
static int filecache_mkdir_parent(const char *);
static int filecache_uncache_node(const char *);

int
filecache_getattr(const char *path, tahoefs_stat_t *tstatp)
{
  assert(path != NULL);
  assert(tstatp != NULL);

  char *remote_infop = NULL;
  size_t remote_info_size;
  char cached_path[MAXPATHLEN];
  FILECACHE_PATH_TO_CACHED_PATH(path, cached_path);
  if (http_stub_get_info(path, &remote_infop, &remote_info_size) == -1) {
    /*
     * tahoe storage doesn't have the specified file or directory.
     * the local cache entry and children (if it is a directory) must
     * be removed here.
     */
    if (filecache_uncache_node(cached_path) == -1) {
      warnx("failed to remove a cache for %s", cached_path);
    }
    return (ENOENT);
  }

  /* convert the infop (in JSON) to tahoefs_stat_t{} structure. */
  if (json_stub_jsonstring_to_tstat(remote_infop, tstatp) == -1) {
    warnx("failed to convert JSON data to tahoefs stat structure");
    free(remote_infop);
    return (EIO);
  }

  /* treat "/" as a special case. */
  if (strcmp(path, "/") == 0) {
    if (filecache_cache_directory(NULL, cached_path, remote_infop,
				  remote_info_size) == -1) {
      warnx("failed to store attr info to the root (/).");
      free(remote_infop);
      return (EIO);
    }
    free(remote_infop);
    return (0);
  }

  if (tstatp->type == TAHOEFS_STAT_TYPE_DIRNODE) {
    /* the specified path at remote storage is a directory. */
    /*
     * the dirnode information doesn't include timestamp information.
     * we need to access the parent directory information (which
     * includes its children information that has timestamp
     * information) to get full information.
     */
    if (filecache_getattr_from_parent(path, tstatp) == -1) {
      return (EIO);
    }

    struct stat cached_stat;
    memset(&cached_stat, 0, sizeof(struct stat));
    if (filecache_get_cache_stat(cached_path, &cached_stat) == -1) {
      if (errno == ENOENT) {
	filecache_cache_directory(NULL, cached_path, remote_infop,
				  remote_info_size);
	free(remote_infop);
	return (0);
      }
      warn("failed to stat %s.", cached_path);
      free(remote_infop);
      return (EIO);
    }

    /* something is cached. */
    if (cached_stat.st_mode & S_IFREG) {
      /* remote is a directory but the local cache is a file. */
      if (filecache_uncache_node(cached_path) == -1) {
	warn("failed to remove cache %s.", cached_path);
	free(remote_infop);
	return (EIO);
      }
    }

    /* cache the latest information. */
    if (filecache_cache_directory(NULL, cached_path, remote_infop,
				  remote_info_size)
	== -1) {
      warn("failed to create a cache directory %s.", cached_path);
      free(remote_infop);
      return (EIO);
    }
  } else {
    /* the specified path at remote storage is a file.*/
    struct stat cached_stat;
    memset(&cached_stat, 0, sizeof(struct stat));
    if (filecache_get_cache_stat(cached_path, &cached_stat) == -1) {
      if (errno == ENOENT) {
	/* just return the latest remote info. */
	free(remote_infop);
	return (0);
      }
      warn("failed to stat %s.", cached_path);
      free(remote_infop);
      return (EIO);
    }
 
    /* something is cached. */
    if (cached_stat.st_mode & S_IFDIR) {
      /* remote is a file but the local cache is a directory. */
      if (filecache_uncache_node(cached_path) == -1) {
	warn("failed to remove cache %s.", cached_path);
	free(remote_infop);
	return (EIO);
      }
    }

    /* check if it is latest or not. */
    int outdated = 0;
    tahoefs_stat_t cached_tstat;
    memset(&cached_tstat, 0, sizeof(tahoefs_stat_t));
    if (filecache_cached_getattr(cached_path, &cached_tstat) == -1) {
      outdated = 1;
    } else {
      if ((tstatp->link_creation_time > cached_tstat.link_creation_time)
	  || (tstatp->link_modification_time
	      > cached_tstat.link_modification_time)) {
	outdated = 1;
      }
    }
    if (outdated) {
      filecache_uncache_node(cached_path);
    }
  }

  free(remote_infop);

  return (0);
}

static int
filecache_getattr_from_parent(const char *path, tahoefs_stat_t *tstatp)
{
  assert(path != NULL);
  assert(tstatp != NULL);

  /* get the parent path. */
  char *parent_path = strdup(path);
  if (parent_path == NULL) {
    warn("failed to duplicate a string (%s).", path);
  }
  char *slash = strrchr(parent_path, '/');
  *slash = '\0';
  const char *child_name = path + strlen(parent_path) +  1;
  if (*parent_path == '\0') {
    /* this means the root directory. */
    *parent_path = '/';
    *(parent_path + 1) = '\0';
  }

  /* get the parent's remote info. */
  char *remote_infop = NULL; /* must free this before returning. */
  size_t remote_info_size;
  char cached_path[MAXPATHLEN];
  FILECACHE_PATH_TO_CACHED_PATH(parent_path, cached_path);
  if (http_stub_get_info(parent_path, &remote_infop, &remote_info_size)
      == -1) {
    /* there is no paranet directory. */
    warnx("parent directory of %s does not exist.", path);
    if (filecache_uncache_node(cached_path) == -1) {
      warnx("failed to remove a cache for %s.", cached_path);
    }
    return (-1);
  }

  /* get the info of the specified child. */
  char *child_infop = NULL; /* must free this before returning. */
  json_stub_extract_child(child_name, &child_infop, remote_infop);
  free(remote_infop);

  /* create a cached directory and store info attr */
  /* if it is a file, ignore it */
  /* also fill tstatp */
  json_stub_jsonstring_to_tstat(child_infop, tstatp);


  free(child_infop);

  return (0);
}

static int
filecache_cached_getattr(const char *cached_path,
			 tahoefs_stat_t *cached_tstatp)
{
  assert(cached_path != NULL);
  assert(cached_tstatp != NULL);

  char *cached_infos = NULL;
  if (filecache_get_info_xattr(cached_path, (void **)&cached_infos) == -1) {
    warnx("failed to get info xattr value from %s.", cached_path);
    return (-1);
  }

  if (json_stub_jsonstring_to_tstat(cached_infos, cached_tstatp) == -1) {
    warnx("failed to convert JSON info string to tahoefs_stat_t{}.");
    free(cached_infos);
    return (-1);
  }
    
  free(cached_infos);

  return (0);
}

static ssize_t
filecache_get_info_xattr(const char *cached_path, void **infopp)
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
    warnx("failed to retreive the size of tahoefs_info attr of %s.",
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
filecache_set_info_xattr(const char *cached_path, void *infop, size_t info_size)
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
filecache_get_real_size(const char *path, size_t *real_size)
{
  assert(path != NULL);
  assert(real_size != NULL);

  char cache_path[MAXPATHLEN];
  FILECACHE_PATH_TO_CACHED_PATH(path, cache_path);

  struct stat stat;
  memset(&stat, 0, sizeof(struct stat));
  if (filecache_get_cache_stat(cache_path, &stat) == -1) {
    if (filecache_cache_file(path, cache_path) == -1) {
      warnx("failed to cache %s.", path);
      return (EIO);
    }
    if (filecache_get_cache_stat(cache_path, &stat) == -1) {
      warnx("failed to get cache stat of %s.", cache_path);
      return (EIO);
    }
  }
  *real_size = stat.st_size;

  return (0);
}

int
filecache_open(const char *path, int flags)
{
  assert(path != NULL);

  /* exclude unsupported options. */
  if (flags && !(flags & FILECACHE_SUPPORTED_OPEN_FLAGS)) {
      return (EINVAL);
  }

  /* when the read op is specified, the specified file must exist. */
  if (flags & (O_RDONLY|O_RDWR)) {
    tahoefs_stat_t tstat;
    memset(&tstat, 0, sizeof(tahoefs_stat_t));
    int errcode = 0;
    errcode = filecache_getattr(path, &tstat);
    if (errcode) {
      /* cannot get attribute of the file. */
      return (errcode);
    }
  }

  return (0);
}

int
filecache_create(const char *path, mode_t mode)
{
  assert(path != NULL);

  char cached_path[MAXPATHLEN];
  FILECACHE_PATH_TO_CACHED_PATH(path, cached_path);

  int fd = open(cached_path, (O_CREAT|O_TRUNC|O_WRONLY), (S_IRUSR|S_IWUSR));
  if (fd == -1) {
    warn("failed to create a file %s", cached_path);
    return (errno);
  }
  close(fd);

  if (http_stub_create(path, cached_path, (mode & S_IWUSR)) == -1) {
    warnx("failed to create the file %s via HTTP", path);
    return (EIO);
  }

  filecache_cache_file(path, cached_path);

  return (0);
}

int
filecache_unlink(const char *path)
{
  assert(path != NULL);

  if (http_stub_unlink_rmdir(path) == -1) {
    warnx("failed to remove a file %s via HTTP", path);
    return (EIO);
  }

  return (0);
}

int
filecache_read(const char *path, char *buf, size_t size, off_t offset,
	       int flags)
{
  assert(path != NULL);
  assert(buf != NULL);

  if (flags & O_WRONLY)
    return (-1);

  char cache_path[MAXPATHLEN];
  FILECACHE_PATH_TO_CACHED_PATH(path, cache_path);

  struct stat stat;
  memset(&stat, 0, sizeof(struct stat));
  if (filecache_get_cache_stat(cache_path, &stat) == -1) {
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

int
filecache_write(const char *path, const char *buf, size_t size, off_t offset,
	       int flags)
{
  assert(path != NULL);
  assert(buf != NULL);

  if (flags & O_RDONLY) {
    warnx("writing to a file opened as read only: %s", path);
    return (-1);
  }

  char cached_path[MAXPATHLEN];
  FILECACHE_PATH_TO_CACHED_PATH(path, cached_path);

  struct stat stat;
  memset(&stat, 0, sizeof(struct stat));
  if (filecache_get_cache_stat(cached_path, &stat) == -1) {
    filecache_cache_file(path, cached_path);
    /* error is ignored. */
  }

  int fd = open(cached_path, O_RDWR);
  if (fd == -1) {
    warn("failed to open a cache file %s.", cached_path);
    return (-1);
  }

  ssize_t nwritten = pwrite(fd, buf, size, offset);
  close (fd);

  return (nwritten);
}

int
filecache_flush(const char *path, int flags)
{
  assert(path != NULL);

  /* read only operation doesn't need to flush anything. */
  if (flags & O_RDONLY) {
    return (0);
  }

  char cached_path[MAXPATHLEN];
  FILECACHE_PATH_TO_CACHED_PATH(path, cached_path);

  if (http_stub_flush(path, cached_path) == -1) {
    warnx("failed to flush the contents of %s", path);
    return (EIO);
  }

  return (0);
}

int
filecache_mkdir(const char *path, mode_t mode)
{
  assert(path != NULL);

  if (http_stub_mkdir(path, (mode & S_IWUSR)) == -1) {
    warnx("failed to create a directory %s via HTTP", path);
    return (-EIO);
  }

  return (0);
}

int
filecache_rmdir(const char *path)
{
  assert(path != NULL);

  if (http_stub_unlink_rmdir(path) == -1) {
    warnx("failed to remove a directory %s via HTTP", path);
    return (EIO);
  }

  return (0);
}

static int
filecache_get_cache_stat(const char *cached_path, struct stat *statp)
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
filecache_cache_file(const char *remote_path, const char *cached_path)
{
  assert(cached_path != NULL);
  assert(remote_path != NULL);

  printf("caching %s to %s\n", remote_path, cached_path);

  if (filecache_mkdir_parent(cached_path) == -1) {
    warnx("failed to create a parent directory of %s.", cached_path);
    return (-1);
  }

  if (http_stub_read_file(remote_path, cached_path) == -1) {
    warnx("failed to cache the contents of the file %s.", remote_path);
    return (-1);
  }

  char *cached_infop = NULL;
  size_t cached_info_size;
  if (http_stub_get_info(remote_path, &cached_infop, &cached_info_size) == -1) {
    warnx("failed to get nodeinfo of the file %s.", remote_path);
    return (-1);
  }
  if (filecache_set_info_xattr(cached_path, cached_infop, cached_info_size)
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
filecache_cache_directory(const char *remote_path, const char *cached_path,
			  char *cached_infop, int cached_info_size)
{
  assert(cached_path != NULL);
  assert(!(remote_path == NULL && cached_infop == NULL));

  if (filecache_mkdir_parent(cached_path) == -1) {
    warnx("failed to create a parent directory of %s.", cached_path);
    return (-1);
  }

  if (mkdir(cached_path, S_IRWXU) == -1) {
    if (errno != EEXIST) {
      warn("failed to create a directory %s", cached_path);
      return (-1);
    }
  }

  char *infop = cached_infop;
  int info_size = 0;
  if (cached_infop == NULL) {
    size_t info_size;
    if (http_stub_get_info(remote_path, &infop, &info_size) == -1) {
      warnx("failed to get nodeinfo of the file %s.", cached_path);
      rmdir(cached_path);
      return (-1);
    }
  } else {
    infop = cached_infop;
    info_size = cached_info_size;
  }

  if (filecache_set_info_xattr(cached_path, infop, info_size) == -1) {
    warnx("failed to set xattr of tahoefs_info attr to %s.", cached_path);
    if (cached_infop == NULL) {
      /* when cached_infop is not specified, we allocate infop in this
	 function.  so free it. */
      free(infop);
    }
    rmdir(cached_path);
    return (-1);
  }

  if (cached_infop == NULL) {
    /* when cached_infop is not specified, we allocate infop in this
       function.  so free it. */
    free(infop);
  }

  return (0);
}

static int
filecache_uncache_node(const char *cached_path)
{
  assert(cached_path != NULL);

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
	if (filecache_uncache_node(child_path) == -1) {
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
