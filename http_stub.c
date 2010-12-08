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
#include <assert.h>

#include "inet_stub.h"

#define REQUEST_READ_FILE "GET /uri/%s HTTP/1.0\n\n"

/*
 * path: tahoe URI: /uri/$FILECAP or /uri/$DIRCAP[SUBDIR../]FILENAME
 * response: contents should be copied to here
 */
int
hstub_read_file(const char *path, u_int8_t **response)
{
  assert(path != NULL);
  assert(response != NULL);
  assert(*response != NULL);

  *request = NULL;

  int request_len;
  request_len = strlen(REQUEST_READ_FILE) + strlen(path);
  char *request = (char *)malloc(request_len);
  if (*request == NULL) {
    warnx("failed to allocate memory for URL.");
    return (-1);
  }
  if (sprintf(request, REQUEST_READ_FILE, path) < 0) {
    warnx("failed to construct HTTP request line.");
    free(request);
    return (-1);
  }

  int handle;
  handle = istub_connect(NULL, NULL);
  if (handle == -1) {
    warnx("failed to connect to the WAPI server.");
    free(request);
    return (-1);
  }

  if (send(handle, request, strlen(request), 0) == -1) {
    warnx("failed to send a request.");
    free(request);
    istub_disconnect(handle);
    return (-1);
  }

  /* free the memory allocated for the request command. */
  free(request);

  /* read HTTP response header. */
  int recv_len;
  char recv_buf[1500];
  while (found || recv_len != -1) {
    recv_len = recv(handle, recv_buf, sizeof(recv_buf));
    if (recv_len == -1) {
      /* XXX */
      istub_disconnect(handle);
      return (-1);
    }

  }
  

  return (0);
}
