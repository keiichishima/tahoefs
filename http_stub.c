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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <err.h>

#include <curl/curl.h>

#include <json.h>

#include "http_stub.h"

#define ROOT_CAP "URI:DIR2:wvttaywiquk5wrofckf4cndppe:zckelfnl24mg55a7rpbrflaomd7brdkpf74uokdxrmvsxwdne5pa"

#define WAPI_DEFAULT_SERVER "localhost"
#define WAPI_DEFAULT_PORT "3456"

#define URL_GETATTR "http://%s:%s/uri/%s%s?t=json"
#define URL_READFILE "http://%s:%s/uri/%s%s"

#define MAX_PATH_LENGTH 1024 /* enough? */

struct http_stub_response_memory {
  u_int8_t *datap;
  size_t size;
};

static int http_stub_get_to_memory(const char *,
				   struct http_stub_response_memory *);
static size_t http_stub_get_to_memory_callback(void *, size_t, size_t,
					       void *);

int
http_stub_init(void)
{

  if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
    warnx("failed to initialize the CURL library.");
    return (-1);
  }

  return (0);
}

/*
 * path: tahoe URI: /uri/$FILECAP or /uri/$DIRCAP[SUBDIR../]FILENAME
 * response: contents should be copied to here
 */
int
http_stub_getattr(const char *path, struct stat *stbufp)
{
  assert(path != NULL);
  assert(stbufp != NULL);

  char tahoe_path[MAX_PATH_LENGTH];
  tahoe_path[0] = '\0';
  snprintf(tahoe_path, sizeof(tahoe_path), URL_GETATTR, WAPI_DEFAULT_SERVER,
	   WAPI_DEFAULT_PORT, ROOT_CAP, path);

  struct http_stub_response_memory response;
  response.datap = malloc(1);
  response.size = 0;
  if (http_stub_get_to_memory(tahoe_path, &response) == -1) {
    warnx("failed to get contents from %s.", path);
    if (response.datap)
      free(response.datap);
    return (-ENOENT);
  }

  /* parse the node information. */
  struct json_object *jnodeinfop;
  jnodeinfop = json_tokener_parse((const char *)response.datap);
  if (jnodeinfop == NULL) {
    warnx("failed to parse the node information in JSON format.");
    free(response.datap);
    return (-ENOENT);
  }

  /* free the memory keeping the HTTP response body. */
  free(response.datap);

  /* nodeinfo_json[0] always specifies nodetype. */
  struct json_object *jnodetypep;
  jnodetypep = json_object_array_get_idx(jnodeinfop, 0);
  if (jnodetypep == NULL) {
    warnx("no node type information exists.");
    json_object_put(jnodeinfop);
    return (-ENOENT);
  }
  const char *nodetype;
  nodetype = json_object_get_string(jnodetypep);
  if (nodetype == NULL) {
    warnx("failed to convert JSON string to string.");
    json_object_put(jnodeinfop);
    return (-ENOENT);
  }
  if (strcmp(nodetype, "dirnode") == 0) {
    stbufp->st_mode = S_IFDIR | 0700;
  } else if (strcmp(nodetype, "filenode") == 0) {
    stbufp->st_mode = S_IFREG | 0600;
  } else {
    warnx("unknown file mode."); 
    json_object_put(jnodeinfop);
    return (-ENOENT);
  }

  /* nodeinfo_json[1] contains node specific data. */
  struct json_object *jfileinfop;
  jfileinfop = json_object_array_get_idx(jnodeinfop, 1);
  if (jfileinfop == NULL) {
    warnx("no fileinfo exists.");
    json_object_put(jnodeinfop);
    return (-ENOENT);
  }
  printf("%s\n", json_object_to_json_string(jfileinfop));
  /* metadata and metadata_tahoe exist only in filenode. */
  struct json_object *jmetadatap = NULL;
  struct json_object *jmetadata_tahoep = NULL;
  if (stbufp->st_mode & S_IFREG) {
    jmetadatap = json_object_object_get(jfileinfop, "metadata");
    if (jmetadatap == NULL) {
      warnx("no metadata exists.");
      json_object_put(jnodeinfop);
      return (-ENOENT);
    }
    jmetadata_tahoep = json_object_object_get(jmetadatap, "tahoe");
    if (jmetadata_tahoep == NULL) {
      warnx("no tahoe metadata exists.");
      json_object_put(jnodeinfop);
      return (-ENOENT);
    }
  }

  /* # of hard links. */
  stbufp->st_nlink = 1;

  /* uid and gid. */
  stbufp->st_uid = getuid();
  stbufp->st_gid = getgid();

  /* size of the node. */
  if (stbufp->st_mode & S_IFREG) {
    /* a regular file. */
    struct json_object *jsizep;
    jsizep = json_object_object_get(jfileinfop, "size");
    if (jsizep == NULL) {
      warnx("no size entry exist.");
      json_object_put(jnodeinfop);
      return (-ENOENT);
    }
    stbufp->st_size = json_object_get_int(jsizep);
  } else {
    /* a directory. */
    stbufp->st_size = 4096;
  }

  /* # of 512B blocks allocated.  does it make any sense to set it? */
  stbufp->st_blocks = 0;

  /* timestamps. */
  if (stbufp->st_mode & S_IFREG) {
    struct json_object *jmtimep = json_object_object_get(jmetadata_tahoep,
							 "linkmotime");
    double mtime;
    mtime = json_object_get_double(jmtimep);
    modf(mtime, &mtime);
    stbufp->st_ctime = stbufp->st_atime = stbufp->st_mtime = (time_t)mtime;
  }

  return (0);
}

static int
http_stub_get_to_memory(const char *url,
			struct http_stub_response_memory *responsep)
{
  assert(url != NULL);
  assert(responsep != NULL);
  assert(responsep->datap != NULL);

  CURL *curl_handle;
  if ((curl_handle = curl_easy_init()) == NULL) {
    warnx("failed to initialize the CURL easy interface.");
    return (-1);
  }

  /* set the URL to read. */
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);
  /* curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1); */

  /* get the information of the specified file node. */
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION,
		   http_stub_get_to_memory_callback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)responsep);
  curl_easy_perform(curl_handle);
  long http_response_code = 0;
  curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_response_code);
  curl_easy_cleanup(curl_handle);

  /* check HTTP response code. */
  if (http_response_code != 200) {
    /* treat all the response codes other than 200 as no existent entry. */
    warnx("received HTTP error response %ld.", http_response_code);
    return (-1);
  }
  if (responsep->datap == NULL) {
    warnx("failed to reallocate response buffer.");
    return (-1);
  }

  return(0);
}

static size_t
http_stub_get_to_memory_callback(void *newdatap, size_t size,
				 size_t nmemb, void *gluep)
{
  assert(newdatap != NULL);
  assert(gluep != NULL);

  size_t real_size = size * nmemb;
  struct http_stub_response_memory *responsep
    = (struct http_stub_response_memory *)gluep;
  responsep->datap = realloc(responsep->datap, responsep->size + real_size + 1);
  if (responsep->datap == NULL) {
    warnx("failed to reallocate memory for http response.");
    return (0);
  }
  memcpy(&(responsep->datap[responsep->size]), newdatap, real_size);
  responsep->size += real_size;
  responsep->datap[responsep->size] = 0;

  return (real_size);
}
