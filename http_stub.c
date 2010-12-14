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

#include "http_stub.h"

#define ROOT_CAP "URI:DIR2:wvttaywiquk5wrofckf4cndppe:zckelfnl24mg55a7rpbrflaomd7brdkpf74uokdxrmvsxwdne5pa"

#define WAPI_DEFAULT_SERVER "localhost"
#define WAPI_DEFAULT_PORT "3456"

#define URL_GET_INFO "http://%s:%s/uri/%s%s?t=json"
#define URL_GETATTR "http://%s:%s/uri/%s%s?t=json"
#define URL_READFILE "http://%s:%s/uri/%s%s"

struct http_stub_response_memory {
  u_int8_t *datap;
  size_t size;
};

static int http_stub_get_to_memory(const char *,
				   struct http_stub_response_memory *);
static size_t http_stub_get_to_memory_callback(void *, size_t, size_t,
					       void *);

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
 * the webapi server, port, and root_cap information is appended
 * within this function.
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

  char tahoe_path[MAXPATHLEN];
  tahoe_path[0] = '\0';
  /* XXX read customized values for server, port, and root_cap. */
  snprintf(tahoe_path, sizeof(tahoe_path), URL_GET_INFO, WAPI_DEFAULT_SERVER,
	   WAPI_DEFAULT_PORT, ROOT_CAP, path);

  struct http_stub_response_memory response;
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
