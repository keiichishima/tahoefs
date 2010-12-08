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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <err.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define WAPI_DEFAULT_SERVER "localhost"
#define WAPI_DEFAULT_PORT "3456"

int
istub_connect(const char *remote, const char *port)
{
  /*
   * if specified, use the specified server address and port,
   * otherwise use the default values.
   */
  if (remote == NULL) {
    remote = WAPI_DEFAULT_SERVER;
  }
  if (port == NULL) {
    port = WAPI_DEFAULT_PORT;
  }

  /* connect to the remote server using a stream socket. */
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  /* query the addrinfo structure. */
  int ret;
  struct addrinfo *result;
  ret = getaddrinfo(remote, port, &hints, &result);
  if (ret != 0) {
    warnx("getaddrinfo: %s", gai_strerror(ret));
    return (-1);
  }

  struct addrinfo *resultp;
  int handle;
  for (resultp = result; resultp != NULL; resultp = resultp->ai_next) {
    /* try to create a socket with the resolved addrinfo. */
    handle = socket(resultp->ai_family, resultp->ai_socktype,
		    resultp->ai_protocol);
    if (handle == -1) {
      /* failed to create a socket. */
#ifdef DEBUG
      warnx("failed to create socket(%d, %d, %d)",
	    resultp->ai_family, resultp->ai_socktype, resultp->ai_protocol);
#endif
      continue;
    }

    /* try to connect to the remote server. */
    if (connect(handle, resultp->ai_addr, resultp->ai_addrlen) != -1) {
      /* succeeded. */
      break;
    }

    /* connect failed. try the next addrinfo. */
    close(handle);
  }

  if (resultp == NULL) {
    /* failed to connect all the resolved addrinfo. */
    warnx("failed to connect any of the remote address.");
    freeaddrinfo(result);
    return (-1);
  }

  /* no longer needed. */
  freeaddrinfo(result);

  return (handle);
}

void
istub_disconnect(int handle)
{
  assert(handle >= 3);

  close(handle);
}
