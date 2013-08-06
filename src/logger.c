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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <event2/event.h>

#ifdef _USE_LIBAV
#include <libavutil/avutil.h>
#endif /* _USE_LIBAV */

#include "logger.h"

/* Format headers used by logger */
#define INFO_FORMAT      "\e[1;32m[INFO]\e[00m %s"
#define INFO_FORMAT_NC   "[INFO] %s"
#define WARN_FORMAT      "\e[1;33m[WARN]\e[00m %s"
#define WARN_FORMAT_NC   "[WARN] %s"
#define ERROR_FORMAT     "\e[1;31m[ERROR]\e[00m %s"
#define ERROR_FORMAT_NC  "[ERROR] %s"
#define DEBUG_FORMAT     "\e[1;36m[DEBUG]\e[00m %s"
#define DEBUG_FORMAT_NC  "[DEBUG] %s"
#define TRACE_FORMAT     "\e[1;36m[TRACE]\e[00m %s"
#define TRACE_FORMAT_NC  "[TRACE] %s"

int logger_use_color = 1;
int logger_show_trace = 0;

static void
_event_log_fn(int severity, const char *msg)
{
	switch (severity) {
	case _EVENT_LOG_DEBUG:
		log_debug(msg);
		break;
	case _EVENT_LOG_MSG:
		log_info(msg);
		break;
	case _EVENT_LOG_WARN:
		log_warning(msg);
		break;
	case _EVENT_LOG_ERR:
		log_error(msg);
		break;
	default:
		log_message((MESSAGE_TYPE)-1, 1, msg);
	}
}

#ifdef _USE_LIBAV
static void
_libav_log_fn(void *avcl, int level, const char *fmt, va_list vl)
{
	if (level >= AV_LOG_PANIC && level <= AV_LOG_FATAL) {
		vlog_message(ERROR, 0, fmt, vl);
	}
#ifdef _DEBUG
	else if (level <= AV_LOG_ERROR && logger_show_trace) {
		vlog_message(TRACE, 0, fmt, vl);
	}
#endif /* _DEBUG */
}
#endif /* _USE_LIBAV */

void
logger_init()
{
	event_set_log_callback(_event_log_fn);
#ifdef _USE_LIBAV
	av_log_set_callback(_libav_log_fn);
#endif /* _USE_LIBAV */
}

static char *
_make_format(MESSAGE_TYPE type, int add_newline, const char *fmt)
{
	char* msg_fmt = NULL;
	const char* fmt_hdr = NULL;
	int fmt_len = 0;

	switch (type) {
	case INFO:
		fmt_hdr = logger_use_color ? INFO_FORMAT : INFO_FORMAT_NC;
		break;
	case WARNING:
		fmt_hdr = logger_use_color ? WARN_FORMAT : WARN_FORMAT_NC;
		break;
	case ERROR:
		fmt_hdr = logger_use_color ? ERROR_FORMAT : ERROR_FORMAT_NC;
		break;
#ifdef _DEBUG
	case DEBUG:
		fmt_hdr = logger_use_color ? DEBUG_FORMAT : DEBUG_FORMAT_NC;
		break;
	case TRACE:
		fmt_hdr = logger_use_color ? TRACE_FORMAT : TRACE_FORMAT_NC;
		break;
#endif /* DEBUG */
	default:
		fmt_hdr = "[UNKNOWN] %s";
		break;
	}

	fmt_len = strlen(fmt_hdr) + strlen(fmt);
	msg_fmt = (char *) malloc(fmt_len);
	if (msg_fmt == NULL) {
		return NULL;
	}
	sprintf(msg_fmt, fmt_hdr, fmt);

	if (add_newline) {
		msg_fmt[fmt_len - 2] = '\n';
		msg_fmt[fmt_len - 1] = '\0';
	}

	return msg_fmt;
}

void
log_message(MESSAGE_TYPE type, int add_newline, const char *fmt, ...)
{
	char *msg_fmt = NULL;
	va_list list;

#ifdef _DEBUG
	if (!logger_show_trace && type == TRACE) {
		return;
	}
#endif

	msg_fmt = _make_format(type, add_newline, fmt);

	va_start(list, fmt);

	if (msg_fmt == NULL) {
		vfprintf(stderr, fmt, list);
	}

	if (type == ERROR) {
		vfprintf(stderr, msg_fmt, list);
	} else {
		vfprintf(stdout, msg_fmt, list);
	}
	va_end(list);

	free(msg_fmt);
}

void
vlog_message(MESSAGE_TYPE type, int add_newline, const char *fmt, va_list vl)
{
	char *msg_fmt = NULL;

#ifdef _DEBUG
	if (!logger_show_trace && type == TRACE) {
		return;
	}
#endif

	msg_fmt = _make_format(type, add_newline, fmt);

	if (msg_fmt == NULL) {
		vfprintf(stderr, fmt, vl);
	}

	if (type == ERROR) {
		vfprintf(stderr, msg_fmt, vl);
	} else {
		vfprintf(stdout, msg_fmt, vl);
	}

	free(msg_fmt);
}
