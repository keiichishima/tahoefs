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
#include <assert.h>
#include <err.h>
#include <sys/time.h>

#include <json.h>

#include "tahoefs.h"

int
json_stub_json_to_metadata(const char *json,
			   struct tahoefs_metadata *metadatap)
{
  assert(json != NULL);
  assert(metadatap != NULL);

  /* parse the node information. */
  struct json_object *jnodeinfop;
  jnodeinfop = json_tokener_parse(json);
  if (jnodeinfop == NULL) {
    warnx("failed to parse the node information in JSON format.");
    return (-1);
  }

  /* the first entry of jnodeinfop always specifies nodetype. */
  struct json_object *jnodetypep;
  jnodetypep = json_object_array_get_idx(jnodeinfop, 0);
  if (jnodetypep == NULL) {
    warnx("no node type information exists.");
    json_object_put(jnodeinfop);
    return (-1);
  }
  const char *nodetype;
  nodetype = json_object_get_string(jnodetypep);
  if (nodetype == NULL) {
    warnx("failed to convert JSON string to string.");
    json_object_put(jnodeinfop);
    return (-1);
  }
  if (strcmp(nodetype, "dirnode") == 0) {
    metadatap->type = TAHOEFS_METADATA_TYPE_DIRNODE;
  } else if (strcmp(nodetype, "filenode") == 0) {
    metadatap->type = TAHOEFS_METADATA_TYPE_FILENODE;
  } else {
    warnx("unknown file mode."); 
    json_object_put(jnodeinfop);
    return (-1);
  }

  /* the second entry of jnodeinfop contains node specific data. */
  struct json_object *jfileinfop;
  jfileinfop = json_object_array_get_idx(jnodeinfop, 1);
  if (jfileinfop == NULL) {
    warnx("no fileinfo exists.");
    json_object_put(jnodeinfop);
    return (-1);
  }

#ifdef DEBUG
  printf("%s\n", json_object_to_json_string(jfileinfop));
#endif

  /* metadata and metadata_tahoe exist only in filenode. */
  struct json_object *jmetap = NULL;
  struct json_object *jmeta_tahoep = NULL;
  jmetap = json_object_object_get(jfileinfop, "metadata");
  if (jmetap) {
    jmeta_tahoep = json_object_object_get(jmetap, "tahoe");
  }

  struct json_object *jsizep;
  jsizep = json_object_object_get(jfileinfop, "size");
  if (jsizep) {
    metadatap->size = json_object_get_int(jsizep);
  } else {
    /* unknown, maybe directory. */
    metadatap->size = 0;
  }

  if (jmeta_tahoep) {
    double time;
    struct json_object *jtimep;
    jtimep = json_object_object_get(jmeta_tahoep, "linkcrtime");
    time = json_object_get_double(jtimep);
    metadatap->link_creation_time = (time_t)time;

    jtimep = json_object_object_get(jmeta_tahoep, "linkmotime");
    time = json_object_get_double(jtimep);
    metadatap->link_modification_time = (time_t)time;
  }

  /* release the parsed JSON structure. */
  json_object_put(jnodeinfop);

  return (0);
}
