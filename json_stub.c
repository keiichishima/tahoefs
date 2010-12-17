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
#include <json_object_private.h>

#include "tahoefs.h"
#include "json_stub.h"

int
json_stub_json_to_metadata(const char *json, tahoefs_stat_t *tstatp)
{
  assert(json != NULL);
  assert(tstatp != NULL);

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
    tstatp->type = TAHOEFS_METADATA_TYPE_DIRNODE;
  } else if (strcmp(nodetype, "filenode") == 0) {
    tstatp->type = TAHOEFS_METADATA_TYPE_FILENODE;
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

  /* "metadata" and "metadata":{"tahoe"} exist only in filenode. */
  struct json_object *jmetap = NULL;
  struct json_object *jmeta_tahoep = NULL;
  jmetap = json_object_object_get(jfileinfop, "metadata");
  if (jmetap) {
    jmeta_tahoep = json_object_object_get(jmetap, "tahoe");
  }

  /* "size" key. */
  struct json_object *jsizep;
  jsizep = json_object_object_get(jfileinfop, "size");
  if (jsizep) {
    tstatp->size = json_object_get_int(jsizep);
  } else {
    /* unknown. maybe directory. */
    tstatp->size = 0;
  }

  /* "mutable" key. */
  struct json_object *jmutablep;
  jmutablep = json_object_object_get(jfileinfop, "mutable");
  if (jmutablep == NULL) {
    warnx("no mutable key exist.");
    json_object_put(jnodeinfop);
    return (-1);
  }
  tstatp->mutable = json_object_get_boolean(jmutablep);

  /* uri keys. */
  struct json_object *jurip;
  jurip = json_object_object_get(jfileinfop, "ro_uri");
  if (jurip == NULL) {
    warnx("no ro_uri key exist.");
    json_object_put(jnodeinfop);
    return (-1);
  }
  strncpy(tstatp->ro_uri, json_object_get_string(jurip),
	  TAHOEFS_CAPABILITY_SIZE);

  jurip = json_object_object_get(jfileinfop, "verify_uri");
  if (jurip == NULL) {
    warnx("no verify_uri key exist.");
    json_object_put(jnodeinfop);
    return (-1);
  }
  strncpy(tstatp->verify_uri, json_object_get_string(jurip),
	  TAHOEFS_CAPABILITY_SIZE);

  jurip = json_object_object_get(jfileinfop, "rw_uri");
  if (jurip) {
    strncpy(tstatp->verify_uri, json_object_get_string(jurip),
	    TAHOEFS_CAPABILITY_SIZE);
  }

  /* "linkcrtime" and "linkmotime" keys. */
  if (jmeta_tahoep) {
    double time;
    struct json_object *jtimep;
    jtimep = json_object_object_get(jmeta_tahoep, "linkcrtime");
    time = json_object_get_double(jtimep);
    tstatp->link_creation_time = (time_t)time;

    jtimep = json_object_object_get(jmeta_tahoep, "linkmotime");
    time = json_object_get_double(jtimep);
    tstatp->link_modification_time = (time_t)time;
  }

  /* release the parsed JSON structure. */
  json_object_put(jnodeinfop);

  return (0);
}

int
json_stub_iterate_children(void *buf, void *fillerp, const char *json,
			   json_stub_iterate_children_callback_t callback)
{
  assert(json != NULL);
  assert(callback != NULL);

  /* parse the dirnode information. */
  struct json_object *jnodeinfop;
  jnodeinfop = json_tokener_parse(json);
  if (jnodeinfop == NULL) {
    warnx("failed to parse the dirnode information in JSON format.");
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
  if (strcmp(nodetype, "dirnode") != 0) {
    warnx("this is not a dirnode.");
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

  /* children exist only in dirnode. */
  struct json_object *jchildrenp = NULL;
  jchildrenp = json_object_object_get(jfileinfop, "children");
  if (jchildrenp == NULL) {
    warnx("no children key in a dirnode fileinfo.");
    json_object_put(jnodeinfop);
    return (-1);
  }

  struct json_object_iter iter;
  json_object_object_foreachC(jchildrenp, iter) {
    tahoefs_readdir_baton_t baton;
    baton.nodename = iter.key;
    baton.infop = json_object_to_json_string(iter.val);
    baton.nodename_listp = buf;
    baton.fillerp = fillerp;
    if (callback(&baton) == -1) {
      warnx("failed to add %s to directory list.", iter.key);
      continue;
    }
  }

  json_object_put(jnodeinfop);

  return (0);
}
