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

#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "logger.h"
#include "config.h"

static const struct {
	cfg_key_t	key;
	const char     *key_str;
	const char     *default_value;
} options_table[] = {
	{ CFG_LISTENING_ADDRESS, "listening-address", DEFAULT_LISTENING_ADDRESS },
	{ CFG_LISTENING_PORT,    "listening-port",    DEFAULT_LISTENING_PORT },
	{ CFG_DOCUMENT_ROOT,     "document-root",     DEFAULT_DOCUMENT_ROOT },
	{ CFG_DATABASE_PATH,     "database-path",     DEFAULT_DB_PATH },
	{ CFG_MONGOOSE_THREADS,  "mongoose-threads",  DEFAULT_MONGOOSE_THREADS },
	{ CFG_MUSIC_DIR,         "music-dir",         DEFAULT_MUSIC_DIR }
};

typedef struct {
	char	*values[CFG_KEY_LAST];
} _cfg_t;

static char *
_strip_leading_whitespace(char *str)
{
	char *p = str;
	while ((*p == ' ' || *p == '\t') && *p != '\n' && *p != '\r') ++p;
	return p;
}

static char *
_strip_trailing_whitechars(char *str)
{
	char *p = str;
	while (*p != '\n' && *p != '\r') {
		if (*p == '\t' || *p == ' ' || *p == '"') {
			*p = '\0';
		}
		p++;
	}
	*p = '\0';
	return str;
}

static int
_is_comment(char *line)
{
	char *cur = _strip_leading_whitespace(line);
	if (*cur == '#' || *cur == '\n' || *cur == '\r') {
		return 1;
	} else {
		return 0;
	}
}

static int
_parse_line(_cfg_t *cfg, char *line)
{
	int i;

	char *key = _strip_leading_whitespace(line);
	if (*key == '\n' || *key == '\r') {
		return -1;
	}

	for (i = 0; i < CFG_KEY_LAST; ++i) {
		const char *opt_key = options_table[i].key_str;
		int key_len = strlen(opt_key);
		if (key_len >= strlen(key)) {
			log_error("Configuration key too short: %s", key);
			return -1;
		}
		if (0 != strncmp(key, opt_key, key_len)) {
			continue;
		}

		char *val = index(line, '=');
		if (val == NULL) {
			log_error("Missing equal sign for option: %s", opt_key);
			return -1;
		}
		val = _strip_leading_whitespace(val + 1);
		if (*val == '"') {
			val++;
		}
		val = _strip_trailing_whitechars(val);
		if (*val == '\n' || *val == '\r') {
			log_error("Missing value for option: %s", opt_key);
			return -1;
		}

		cfg->values[i] = strdup(val);
	}

	return 0;
}

static int
_parse_config_file(_cfg_t *cfg, const char *file)
{
	char *lineptr = NULL;
	size_t line_len;

	FILE *cf = fopen(file, "r");
	if (cf == NULL) {
		log_error("Failed to open configuration file: %s", file);
		return -1;
	}

	log_debug("Parsing config file: %s", file);

	errno = 0;
	while (-1 != getline(&lineptr, &line_len, cf)) {
		if (_is_comment(lineptr)) {
			free(lineptr);
			lineptr = NULL;
			continue;
		}
		if (0 != _parse_line(cfg, lineptr)) {
			log_error("Failed to parse config line: %s", lineptr);
			free(lineptr);
			lineptr = NULL;
			break;
		}
		free(lineptr);
		lineptr = NULL;
		errno = 0;
	}

	if (lineptr != NULL) {
		free(lineptr);
	}

	fclose(cf);
	if (errno == EINVAL) {
		log_error("Failed to parse configuration file: %s", file);
		return -1;
	}

	log_debug("Configuration file parsed successfully");

	return 0;
}

cfg_t *
cfg_init(const char* cfg_path)
{
	log_info("Reading configuration from: %s", cfg_path);

	_cfg_t *_cfg = NULL;
	_cfg = malloc (sizeof(_cfg_t));
	memset(_cfg, 0, sizeof(_cfg_t));
	if (_cfg == NULL) {
		log_error("Failed to allocate memory for program configuration!");
		return NULL;
	}

	if (0 != _parse_config_file(_cfg, cfg_path)) {
		free(_cfg);
		return NULL;
	}

#ifdef _DEBUG
	int idx;
	log_debug("Configuration:");
	for (idx = 0; idx < CFG_KEY_LAST; idx++) {
		log_debug("\t%s = %s", options_table[idx].key_str, cfg_get_str(_cfg, idx));
	}
#endif

	return _cfg;
}

void
cfg_free(cfg_t *cfg)
{
	_cfg_t *_cfg = (_cfg_t *)cfg;
	int i;

	for (i = 0; i < CFG_KEY_LAST; i++) {
		if (_cfg->values[i] != NULL) {
			free(_cfg->values[i]);
		}
	}

	free(_cfg);
}

const char*
cfg_get_str(cfg_t *cfg, cfg_key_t key)
{
	_cfg_t *_cfg = (_cfg_t *)cfg;

	assert(key < CFG_KEY_LAST);

	if (_cfg->values[key] == NULL) {
		return options_table[key].default_value;
	} else {
		return _cfg->values[key];
	}
}
