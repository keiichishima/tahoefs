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
#define URL_READ_FILE "http://%s:%s/uri/%s%s"

typedef struct http_stub_get_baton {
  u_int8_t *datap;
  size_t size;
} http_stub_get_baton_t;

static int http_stub_get_to_memory(const char *, http_stub_get_baton_t *);
static size_t http_stub_get_to_memory_callback(void *, size_t, size_t,
					       void *);
static int http_stub_get_to_file(const char *, const char *);
static size_t http_stub_get_to_file_callback(void *, size_t, size_t, void *);

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

  char tahoe_path[MAXPATHLEN]; /* XXX enough? */
  tahoe_path[0] = '\0';
  snprintf(tahoe_path, sizeof(tahoe_path), URL_GET_INFO, config.webapi_server,
	   config.webapi_port, config.root_cap, path);

  http_stub_get_baton_t response;
  response.datap = malloc(1);
  response.size = 0;
  if (http_stub_get_to_memory(tahoe_path, &response) == -1) {
    warnx("failed to get contents from %s.", tahoe_path);
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
http_stub_get_to_memory(const char *url, http_stub_get_baton_t *responsep)
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
			 http_stub_get_to_memory_callback);
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
#ifdef DEBUG
    warnx("received HTTP error response %ld.", response_code);
#endif
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
http_stub_get_to_memory_callback(void *newdatap, size_t size, size_t nmemb,
				 void *batonp)
{
  assert(newdatap != NULL);
  assert(batonp != NULL);

  size_t real_size = size * nmemb;
  http_stub_get_baton_t *responsep = (http_stub_get_baton_t *)batonp;
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

  char tahoe_path[MAXPATHLEN];
  tahoe_path[0] = '\0';
  snprintf(tahoe_path, sizeof(tahoe_path), URL_READ_FILE, config.webapi_server,
	   config.webapi_port, config.root_cap, path);

  if (http_stub_get_to_file(tahoe_path, local_path) == -1) {
    warnx("failed to get contents from %s.", tahoe_path);
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
#ifdef DEBUG
    warnx("received HTTP error response %ld.", response_code);
#endif
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
