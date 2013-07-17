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
#include <string.h>

#include <libdaemon/dlog.h>

#include "configuration.h"
#include "config.h"

static void
cfg_to_libdaemon_errfunc(cfg_t *cfg, const char *fmt, va_list ap)
{
	daemon_logv(LOG_ERR, fmt, ap);
}

cfg_t *
configuration_init(const char* cfg_path)
{
	cfg_opt_t opts[] = {
		CFG_STR("listening-ports", DEFAULT_LISTENING_PORTS, CFGF_NONE),
		CFG_STR("document-root", DEFAULT_DOCUMENT_ROOT, CFGF_NONE),
		CFG_STR("database-file", DEFAULT_DB_PATH, CFGF_NONE),
		CFG_STR("mongoose-threads", DEFAULT_MONGOOSE_THREADS, CFGF_NONE),
		CFG_STR_LIST("music-dirs", 0, CFGF_NODEFAULT),
		CFG_END()
	};

	daemon_log(LOG_INFO, "Reading configuration from: %s", cfg_path);

	cfg_t *cfg = cfg_init(opts, CFGF_NONE);
	if (!cfg) {
		return NULL;
	}

	(void)cfg_set_error_function(cfg, &cfg_to_libdaemon_errfunc);

	switch (cfg_parse(cfg, cfg_path)) {
	case CFG_SUCCESS:
		break;
	case CFG_FILE_ERROR:
		daemon_log(LOG_ERR, "Configuration file could not be read: %s", strerror(errno));
		return NULL;
	case CFG_PARSE_ERROR:
		daemon_log(LOG_ERR, "Configuration file could not be parsed!");
		return NULL;
	default:
		daemon_log(LOG_ERR, "Unknown configuration file parsing status!\n");
		return NULL;
	}

	return cfg;
}

void
configuration_free(cfg_t *cfg)
{
	cfg_free(cfg);
}
