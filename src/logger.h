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

#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Format headers used by logger */
#define INFO_FORMAT		"\e[1;32m[INFO]\e[00m %s\n"
#define INFO_FORMAT_NC		"[INFO] %s\n"
#define WARN_FORMAT    		"\e[1;33m[WARNING]\e[00m %s\n"
#define WARN_FORMAT_NC 		"[WARNING] %s\n"
#define ERROR_FORMAT   		"\e[1;31m[ERROR]\e[00m %s\n"
#define ERROR_FORMAT_NC		"[ERROR] %s\n"
#define DEBUG_FORMAT   		"\e[1;36m[DEBUG]\e[00m %s\n"
#define DEBUG_FORMAT_NC		"[DEBUG] %s\n"

/* Supported message types.*/
typedef enum {
	INFO,
	WARNING,
	ERROR,
#ifdef DEBUG
	DBG,
	DBG2,
#endif /* DEBUG */
} MESSAGE_TYPE;

/**
 * Main logging routine. Should not be used directly.
 * Please use log_<type> macros.
 * @param type Type of message to log
 * @param format Message format
 */
void
log_message(MESSAGE_TYPE type, const char* format, ...);

/* Logging macros. */
#define log_info(format, args...)    log_message(INFO, format, ## args)
#define log_warning(format, args...) log_message(WARNING, format, ## args)
#define log_error(format, args...)   log_message(ERROR, format, ## args)
#ifdef DEBUG
#define log_debug(format, args...)   log_message(DBG, format, ## args)
#ifdef DEBUG2
#define log_debug2(format, args...)   log_message(DBG2, format, ## args)
#else
#define log_debug2(format, args...)
#endif /* DEBUG2 */
#else
#define log_debug(format, args...)
#define log_debug2(format, args...)
#endif /* DEBUG */

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* LOGGER_H */

