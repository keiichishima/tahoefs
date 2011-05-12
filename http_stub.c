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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <err.h>

#include <curl/curl.h>

#include "tahoefs.h"
#include "http_stub.h"

#define URL_GET_INFO "http://%s:%s/uri/%s%s?t=json"
#define URL_CREATE "http://%s:%s/uri/%s%s%s"
#define URL_READ_FILE "http://%s:%s/uri/%s%s"
#define URL_WRITE_FILE "http://%s:%s/uri/%s%s"
#define URL_WRITE_FILE2 "http://%s:%s/uri/%s%s%s"
#define URL_MKDIR "http://%s:%s/uri/%s%s%s"
#define URL_RMDIR "http://%s:%s/uri/%s%s"

typedef struct http_stub_writefunc_baton {
  u_int8_t *datap;
  size_t size;
} http_stub_writefunc_baton_t;

static int http_stub_get_to_memory(const char *, http_stub_writefunc_baton_t *);
static size_t http_stub_writefunc_callback(void *, size_t, size_t, void *);
static int http_stub_get_to_file(const char *, const char *);
static size_t http_stub_get_to_file_callback(void *, size_t, size_t, void *);
static int http_stub_put(const char *, http_stub_writefunc_baton_t *);
static int http_stub_delete(const char *);
static int http_stub_put_from_file(const char *, const char *,
				   http_stub_writefunc_baton_t *);
static size_t http_stub_put_from_file_callback(void *, size_t, size_t, void *);
static int http_stub_post_from_file(const char *, const char *);

int
http_stub_initialize(void)
{
  if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
    warnx("failed to initialize the CURL library.");
    return (-1);
  }

  return (0);
}

int
http_stub_terminate(void)
{
  curl_global_cleanup();

  return (0);
}

/*
 * issue a HTTP GET request to get filenode or dirnode information stored
 * in the tahoe storage related to the location specified as the path
 * parameter.
 *
 * infopp and info_sizep will be filled with the HTTP GET response
 * body and the length of the response message.  THE CALLER MUST FREE
 * THE MEMORY allocated to the infopp parameter.
 */
int
http_stub_get_info(const char *path, char **infopp, size_t *info_sizep)
{
  assert(path != NULL);
  assert(infopp != NULL);
  assert(*infopp == NULL);
  assert(info_sizep != NULL);

  char tahoe_url[MAXPATHLEN]; /* XXX enough? */
  tahoe_url[0] = '\0';
  snprintf(tahoe_url, sizeof(tahoe_url), URL_GET_INFO, config.webapi_server,
	   config.webapi_port, config.root_cap, path);

  http_stub_writefunc_baton_t response;
  response.datap = malloc(1);
  response.size = 0;
  if (http_stub_get_to_memory(tahoe_url, &response) == -1) {
    warnx("failed to get contents from %s.", tahoe_url);
    if (response.datap)
      free(response.datap);
    return (-1);
  }
  *infopp = (char *)response.datap;
  *info_sizep = response.size;

  return (0);
}

/*
 * call CURL functions to get the contents of the url specified as the
 * url parameter.  the response will be stored in the memory space
 * specified by the responsep->datap parameter.
 */
static int
http_stub_get_to_memory(const char *url, http_stub_writefunc_baton_t *responsep)
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
  CURLcode ret;
  ret = curl_easy_setopt(curl_handle, CURLOPT_URL, url);
  if (ret != CURLE_OK) {
    warnx("failed to set URL %s. (CURL: %s)", url, curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  /* get the information of the specified file node. */
  ret = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION,
			 http_stub_writefunc_callback);
  if (ret != CURLE_OK) {
    warnx("failed to set write function for memory. (CURL: %s)",
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }
  ret = curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)responsep);
  if (ret != CURLE_OK) {
    warnx("failed to set callback baton for memory. (CURL: %s)",
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }
  ret = curl_easy_perform(curl_handle);
  if (ret != CURLE_OK) {
    warnx("failed to perform CURL operation for %s. (CURL: %s)",
	  url, curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  long response_code = 0;
  ret = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE,
			  &response_code);
  if (ret != CURLE_OK) {
    warnx("failed to get CURL operation response code for %s. (CURL: %s)",
	  url, curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  curl_easy_cleanup(curl_handle);

  /* check HTTP response code. */
  if (response_code != 200) {
    /* treat all the response codes other than 200 as no existent entry. */
    warnx("received HTTP error response %ld.", response_code);
    return (-1);
  }
  if (responsep->datap == NULL) {
    warnx("failed to reallocate memory for HTTP response.");
    return (-1);
  }

  return(0);
}

/*
 * the callback function of the http_stub_get_to_memory() function.
 * every time the CURL library receives a part of the response
 * message, this function is called.  the storage passed as a
 * batonp->datap will be enlarged whenever necessary.
 */
static size_t
http_stub_writefunc_callback(void *newdatap, size_t size, size_t nmemb,
			     void *batonp)
{
  assert(newdatap != NULL);
  assert(batonp != NULL);

  size_t real_size = size * nmemb;
  http_stub_writefunc_baton_t *responsep
    = (http_stub_writefunc_baton_t *)batonp;
  responsep->datap = realloc(responsep->datap, responsep->size + real_size + 1);
  if (responsep->datap == NULL) {
    warnx("failed to reallocate memory for HTTP response.");
    return (0);
  }
  memcpy(&(responsep->datap[responsep->size]), newdatap, real_size);
  responsep->size += real_size;
  responsep->datap[responsep->size] = 0;

  return (real_size);
}

int
http_stub_create(const char *path, const char *local_path, int ismutable)
{
  assert(path != NULL);
  assert(local_path != NULL);

  const char *create_opt;
  if (ismutable)
    create_opt = "?mutable=true";
  else
    create_opt = "";

  char tahoe_url[MAXPATHLEN];
  tahoe_url[0] = '\0';
  snprintf(tahoe_url, sizeof(tahoe_url), URL_CREATE, config.webapi_server,
	   config.webapi_port, config.root_cap, path, create_opt);

  /* response is ignored though. */
  http_stub_writefunc_baton_t response;
  response.datap = malloc(1);
  response.size = 0;
  if (http_stub_put_from_file(tahoe_url, local_path, &response) == -1) {
    warnx("failed to issue a PUT request for URL %s", tahoe_url);
    return (-1);
  }
  if (response.datap)
    free(response.datap);

  return (0);
}

/*
 * issue a HTTP GET request to get the content of a filenode stored in
 * the tahoe storage related to the location specified as the path
 * parameter.  the received content will be saved at the local_path of
 * the local filesystem.
 */
int
http_stub_read_file(const char *path, const char *local_path)
{
  assert(path != NULL);
  assert(local_path != NULL);

  char tahoe_url[MAXPATHLEN];
  tahoe_url[0] = '\0';
  snprintf(tahoe_url, sizeof(tahoe_url), URL_READ_FILE, config.webapi_server,
	   config.webapi_port, config.root_cap, path);

  if (http_stub_get_to_file(tahoe_url, local_path) == -1) {
    warnx("failed to get contents from %s.", tahoe_url);
    return (-1);
  }

  return (0);
  
}

/*
 * call CURL functions to get the contents of the url specified as the
 * url parameter.  the response will be stored at the path specified
 * by the local_path parameter.
 */
static int
http_stub_get_to_file(const char *url, const char *local_path)
{
  assert(url != NULL);
  assert(local_path != NULL);

  CURL *curl_handle;
  if ((curl_handle = curl_easy_init()) == NULL) {
    warnx("failed to initialize the CURL easy interface.");
    return (-1);
  }

  /* set the URL to read. */
  CURLcode ret;
  ret = curl_easy_setopt(curl_handle, CURLOPT_URL, url);
  if (ret != CURLE_OK) {
    warnx("failed to set URL %s. (CURL: %s)", url, curl_easy_strerror(ret));
    return (-1);
  }
  ret = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION,
			 http_stub_get_to_file_callback);
  if (ret != CURLE_OK) {
    warnx("failed to set write function for memory. (CURL: %s)",
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }
  /* open the specified file to store the returned HTTP content. */
  FILE *fp = fopen(local_path, "w");
  if (fp == NULL) {
    warn("failed to open %s to receive HTTP response.", local_path);
    curl_easy_cleanup(curl_handle);
    return (-1);
  }
  ret = curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, fp);
  if (ret != CURLE_OK) {
    warnx("failed to set file pointer to store HTTP content. (CURL: %s)",
	  curl_easy_strerror(ret));
    fclose(fp);
    curl_easy_cleanup(curl_handle);
    return (-1);
  }
  ret = curl_easy_perform(curl_handle);
  if (ret != CURLE_OK) {
    warnx("failed to perform CURL operation for %s. (CURL: %s)",
	  url, curl_easy_strerror(ret));
    fclose(fp);
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  fclose(fp);

  long response_code = 0;
  curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
  curl_easy_cleanup(curl_handle);

  /* check HTTP response code. */
  if (response_code != 200) {
    /* treat all the response codes other than 200 as an error. */
    warnx("received HTTP error response %ld.", response_code);
    /* remove an incomplete file. */
    unlink(local_path);
    return (-1);
  }

  return(0);
}

/*
 * the callback function of the http_stub_get_to_file() function.
 * every time the CURL library receives a part of the response
 * message, this function is called.  the received data will be
 * appended to the open file specified as the fp parameter.
 */
static size_t
http_stub_get_to_file_callback(void *newdatap, size_t size, size_t nmemb,
			       void *stream)
{
  int written = fwrite(newdatap, size, nmemb, (FILE *)stream);
  return (written);
}

int
http_stub_mkdir(const char *path, int ismutable)
{
  assert(path != NULL);

  const char *mkdir_opt;
  if (ismutable)
    mkdir_opt = "?t=mkdir";
  else
    mkdir_opt = "?t=mkdir-immutable";

  char tahoe_url[MAXPATHLEN];
  tahoe_url[0] = '\0';
  snprintf(tahoe_url, sizeof(tahoe_url), URL_MKDIR, config.webapi_server,
	   config.webapi_port, config.root_cap, path, mkdir_opt);

  /* response is ignored though. */
  http_stub_writefunc_baton_t response;
  response.datap = malloc(1);
  response.size = 0;
  if (http_stub_put(tahoe_url, &response) == -1) {
    warnx("failed to issue a PUT request for URL %s (%s)", tahoe_url);
    return (-1);
  }

  return (0);
}

static int
http_stub_put(const char *url, http_stub_writefunc_baton_t *responsep)
{
  assert(url != NULL);

  CURL *curl_handle;
  if ((curl_handle = curl_easy_init()) == NULL) {
    warnx("failed to initialize the CURL easy interface.");
    return (-1);
  }

  CURLcode ret;
  ret = curl_easy_setopt(curl_handle, CURLOPT_UPLOAD, 1L);
  if (ret != CURLE_OK) {
    warnx("failed to specifu UPLOAD option (CURL: %s)",
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_setopt(curl_handle, CURLOPT_URL, url);
  if (ret != CURLE_OK) {
    warnx("failed to set URL %s. (CURL: %s)", url, curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_setopt(curl_handle, CURLOPT_INFILESIZE, 0);
  if (ret != CURLE_OK) {
    warnx("failed to set filesize 0. (CURL: %s)", url, curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION,
			 http_stub_writefunc_callback);
  if (ret != CURLE_OK) {
    warnx("failed to set write function for memory. (CURL: %s)",
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }
  ret = curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)responsep);
  if (ret != CURLE_OK) {
    warnx("failed to set callback baton for memory. (CURL: %s)",
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_perform(curl_handle);
  if (ret != CURLE_OK) {
    warnx("failed to perform CURL operation for %s. (CURL: %s)",
	  url, curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  curl_easy_cleanup(curl_handle);

  return (0);
}

int
http_stub_unlink_rmdir(const char *path)
{
  assert(path != NULL);

  char tahoe_url[MAXPATHLEN];
  tahoe_url[0] = '\0';
  snprintf(tahoe_url, sizeof(tahoe_url), URL_RMDIR, config.webapi_server,
	   config.webapi_port, config.root_cap, path);

  if (http_stub_delete(tahoe_url) == -1) {
    warnx("failed to issue a DELETE request for URL %s", tahoe_url);
    return (-1);
  }

  return (0);
}

static int
http_stub_delete(const char *url)
{
  assert(url != NULL);

  CURL *curl_handle;
  if ((curl_handle = curl_easy_init()) == NULL) {
    warnx("failed to initialize the CURL easy interface.");
    return (-1);
  }

  CURLcode ret;
  ret = curl_easy_setopt(curl_handle, CURLOPT_URL, url);
  if (ret != CURLE_OK) {
    warnx("failed to set URL %s. (CURL: %s)", url, curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
  if (ret != CURLE_OK) {
    warnx("failed to set DELETE operation. (CURL: %s)", url,
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_perform(curl_handle);
  if (ret != CURLE_OK) {
    warnx("failed to perform CURL operation for %s. (CURL: %s)",
	  url, curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  curl_easy_cleanup(curl_handle);

  return (0);
}

int
http_stub_flush(const char *path, const char *local_path)
{
  assert(path != NULL);
  assert(local_path != NULL);

  char tahoe_url[MAXPATHLEN];
  tahoe_url[0] = '\0';
  snprintf(tahoe_url, sizeof(tahoe_url), URL_WRITE_FILE, config.webapi_server,
	   config.webapi_port, config.root_cap, path);

  /* response is ignored though. */
  http_stub_writefunc_baton_t response;
  response.datap = malloc(1);
  response.size = 0;
  if (http_stub_put_from_file(tahoe_url, local_path, &response) == -1) {
    warnx("failed to issue a PUT request for URL %s", tahoe_url);
    return (-1);
  }
  if (response.datap)
    free(response.datap);

  return (0);
}

static int
http_stub_put_from_file(const char *url, const char *path,
			http_stub_writefunc_baton_t *responsep)
{
  assert(url != NULL);
  assert(path != NULL);

  CURL *curl_handle;
  if ((curl_handle = curl_easy_init()) == NULL) {
    warnx("failed to initialize the CURL easy interface.");
    return (-1);
  }

  CURLcode ret;
  ret = curl_easy_setopt(curl_handle, CURLOPT_READFUNCTION,
			 http_stub_put_from_file_callback);
  if (ret != CURLE_OK) {
    warnx("failed to specify PUT callback function (CURL: %s)",
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_setopt(curl_handle, CURLOPT_UPLOAD, 1L);
  if (ret != CURLE_OK) {
    warnx("failed to specify UPLOAD option (CURL: %s)",
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_setopt(curl_handle, CURLOPT_URL, url);
  if (ret != CURLE_OK) {
    warnx("failed to set URL %s. (CURL: %s)", url, curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION,
			 http_stub_writefunc_callback);
  if (ret != CURLE_OK) {
    warnx("failed to set write function for memory. (CURL: %s)",
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }
  ret = curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)responsep);
  if (ret != CURLE_OK) {
    warnx("failed to set callback baton for memory. (CURL: %s)",
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  struct stat path_stat;
  int stat_fd;
  if ((stat_fd = open(path, O_RDONLY)) == -1) {
    warn("failed to stat upload source file %s", path);
    curl_easy_cleanup(curl_handle);
    return (-1);
  }
  fstat(stat_fd, &path_stat);
  close(stat_fd);

  FILE *path_fd = fopen(path, "rb");
  if (path_fd == NULL) {
    warn("failed to open upload source file %s", path);
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_setopt(curl_handle, CURLOPT_READDATA, path_fd);
  if (ret != CURLE_OK) {
    warnx("failed to set path source fd for %s. (CURL: %s)", url,
	  curl_easy_strerror(ret));
    fclose(path_fd);
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_setopt(curl_handle, CURLOPT_INFILESIZE_LARGE,
			 (curl_off_t)path_stat.st_size);
  if (ret != CURLE_OK) {
    warnx("failed to set filesize of %s. (CURL: %s)", url,
	  curl_easy_strerror(ret));
    fclose(path_fd);
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_perform(curl_handle);
  if (ret != CURLE_OK) {
    warnx("failed to perform CURL operation for %s. (CURL: %s)",
	  url, curl_easy_strerror(ret));
    fclose(path_fd);
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  fclose(path_fd);

  curl_easy_cleanup(curl_handle);

  return (0);
}

static size_t http_stub_put_from_file_callback(void *ptr, size_t size,
					       size_t nmemb, void *stream)
{
  size_t nread;
 
  nread = fread(ptr, size, nmemb, stream);
 
  return (nread);
}

static int
http_stub_post_from_file(const char *url, const char *path)
{
  assert(url != NULL);
  assert(path != NULL);

  struct curl_httppost *formpost = NULL;
  struct curl_httppost *lastptr = NULL;
  struct curl_slist *headerlist = NULL;
  static const char buf[] = "Expect:";

  curl_formadd(&formpost, &lastptr,
	       CURLFORM_COPYNAME, "sendfile",
	       CURLFORM_FILE, path,
	       CURLFORM_END);
  curl_formadd(&formpost, &lastptr,
	       CURLFORM_COPYNAME, "filename",
	       CURLFORM_COPYCONTENTS, path,
	       CURLFORM_END);
  curl_formadd(&formpost, &lastptr,
	       CURLFORM_COPYNAME, "submit",
	       CURLFORM_COPYCONTENTS, "Upload",
	       CURLFORM_END);

  CURL *curl_handle;
  if ((curl_handle = curl_easy_init()) == NULL) {
    warnx("failed to initialize the CURL easy interface.");
    return (-1);
  }

  headerlist = curl_slist_append(headerlist, buf);

  CURLcode ret;
  ret = curl_easy_setopt(curl_handle, CURLOPT_URL, url);
  if (ret != CURLE_OK) {
    warnx("failed to set URL %s. (CURL: %s)", url, curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_setopt(curl_handle, CURLOPT_HTTPPOST, formpost);
  if (ret != CURLE_OK) {
    warnx("failed to specify POST parameters (CURL: %s)",
	  curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  ret = curl_easy_perform(curl_handle);
  if (ret != CURLE_OK) {
    warnx("failed to perform CURL operation for %s. (CURL: %s)",
	  url, curl_easy_strerror(ret));
    curl_easy_cleanup(curl_handle);
    return (-1);
  }

  curl_easy_cleanup(curl_handle);

  return (0);
}
