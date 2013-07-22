/*-
 * Copyright (c) 2013 Peter Tworek
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <tag_c.h>

#include "music_tag.h"
#include "logger.h"

typedef struct
{
	music_tag_t     base;
	TagLib_File    *file;
} music_tag_taglib_t;

music_tag_t *
music_tag_create(const char *path)
{
	const TagLib_AudioProperties *props = NULL;
	TagLib_File *file = NULL;
	TagLib_Tag *tag = NULL;
	music_tag_taglib_t *ret = NULL;

	if ((file = taglib_file_new(path)) == NULL || !taglib_file_is_valid(file)) {
		log_debug("Unrecoginzed file type: %s", path);
		return NULL;
	}

	if ((tag = taglib_file_tag(file)) == NULL) {
		log_warning("Could not read tags from: %s", path);
		goto failure;
	}

	if ((props = taglib_file_audioproperties(file)) == NULL) {
		log_warning("Could not read audio properties from: %s", path);
		goto failure;
	}

	ret = malloc(sizeof(music_tag_taglib_t));
	if (tag == NULL) {
		log_error("Failed to allocate memory or music tag!");
		goto failure;
	}

	ret->base.artist = taglib_tag_artist(tag);
	ret->base.title = taglib_tag_title(tag);
	ret->base.album = taglib_tag_album(tag);
	ret->base.track = taglib_tag_track(tag);
	ret->base.length = taglib_audioproperties_length(props);
	ret->file = file;

	log_trace("TAG: %s -> (%s, %s, %s, %d, %d)", path, ret->base.artist, ret->base.album,
	          ret->base.title, ret->base.length, ret->base.track);

	return (music_tag_t *)ret;

failure:
	if (file) {
		taglib_file_free(file);
	}
	taglib_tag_free_strings();
	return NULL;
}

void
music_tag_destroy(music_tag_t *tag)
{
	music_tag_taglib_t *_tag = (music_tag_taglib_t *)tag;
	taglib_file_free(_tag->file);
	taglib_tag_free_strings();
	free(_tag);
}
