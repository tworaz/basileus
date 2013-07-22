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

#include <libavformat/avformat.h>

#include "music_tag.h"
#include "logger.h"

typedef struct
{
	music_tag_t      base;
	AVFormatContext *ctx;
} _music_tag_libav_t;

static char *
_strip_tailing_whitespace(char *str)
{
	int end = strlen(str);
	--end;

	while (str[end] == '\t' || str[end] == ' ') {
		str[end] = '\0';
		--end;
	}

	return str;
}

music_tag_t *
music_tag_create(const char *file)
{
	AVFormatContext* container = NULL;
	AVDictionaryEntry *tag = NULL;
	_music_tag_libav_t *ret = NULL;
	int i;

	static int libav_initialized = 0;
	if (!libav_initialized) {
		av_log_set_level(AV_LOG_QUIET);
		av_register_all();
		libav_initialized = 1;
	}

	if (avformat_open_input(&container, file, NULL, NULL) < 0) {
		log_debug("Could not open file: %s", file);
		return NULL;
	}

	if (avformat_find_stream_info(container, NULL) < 0) {
		log_debug("Could not find file info: %s", file);
		goto failure;
	}

	int audio_streams = 0;
	for (i = 0; i < container->nb_streams; i++) {
		if (container->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_streams++;
			break;
		}
	}

	if (audio_streams == 0) {
		log_debug("No audio streams found in file: %s", file);
		goto failure;
	}

	ret = malloc(sizeof(_music_tag_libav_t));
	if (ret == NULL)  {
		log_error("Failed to allocate memory for music tag!");
		goto failure;
	}

	tag = av_dict_get(container->metadata, "artist", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag == NULL) {
		log_debug("Failed to read artist field from: %s", file);
		goto failure;
	}
	ret->base.artist = _strip_tailing_whitespace(tag->value);

	tag = av_dict_get(container->metadata, "title", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag == NULL) {
		log_debug("Failed to read title field from: %s", file);
		goto failure;
	}
	ret->base.title = _strip_tailing_whitespace(tag->value);

	tag = av_dict_get(container->metadata, "album", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag == NULL) {
		log_debug("Failed to read album field from: %s", file);
		goto failure;
	}
	ret->base.album = _strip_tailing_whitespace(tag->value);

	tag = av_dict_get(container->metadata, "track", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag != NULL) {
		ret->base.track = atoi(tag->value);
	} else {
		ret->base.track = 0;
	}

	ret->base.length = (int)(container->duration / AV_TIME_BASE);

	ret->ctx = container;

	return (music_tag_t *)ret;

failure:
	if (container) {
		avformat_close_input(&container);
	}
	return NULL;
}

void
music_tag_destroy(music_tag_t *tag)
{
	_music_tag_libav_t *t = (_music_tag_libav_t *) tag;
	avformat_close_input(&(t->ctx));
	free(t);
}
