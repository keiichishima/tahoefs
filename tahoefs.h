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

#ifndef _TAHOEFS_H_
#define _TAHOEFS_H_

#define TAHOEFS_CAPABILITY_SIZE 128
typedef struct tahoefs_global_config {
  const char *tahoe_dir;
  const char *root_cap;
  const char *webapi_server;
  const char *webapi_port;
  const char *filecache_dir;
  int debug;
} tahoefs_global_config_t;
extern tahoefs_global_config_t config;

#define TAHOEFS_STAT_TYPE_UNKNOWN	0
#define TAHOEFS_STAT_TYPE_DIRNODE	1
#define TAHOEFS_STAT_TYPE_FILENODE	2

typedef struct tahoefs_stat {
  int type;
  char rw_uri[TAHOEFS_CAPABILITY_SIZE];
  char ro_uri[TAHOEFS_CAPABILITY_SIZE];
  char verify_uri[TAHOEFS_CAPABILITY_SIZE];
  size_t size;
  int mutable;
  double link_creation_time;
  double link_modification_time;
} tahoefs_stat_t;

typedef struct tahoefs_readdir_baton {
  const char *nodename;
  const char *infop;
  void *nodename_listp;
  void *fillerp;
} tahoefs_readdir_baton_t;

#define DEBUG(format) do {		\
    if (config.debug) printf(format);	\
  } while (0);
#define DEBUGV(format, args...) do {		\
    if (config.debug) printf(format, args);	\
  } while (0);

#endif

