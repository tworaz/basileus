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

#include <stdio.h>
#include <unistd.h>

#include "basileus.h"
#include "logger.h"
#include "config.h"

static void
print_version()
{
	printf("Basileus version %d.%d\n", BASILEUS_VERSION_MAJOR, BASILEUS_VERSION_MINOR);
}

static void
print_help(const char *progname)
{
	printf("Usage: %s [options]\n"
	       "  Available options:\n"
	       "  -c <file>   Read program configuration from specified file\n"
	       "  -n          Disable colors in log output\n"
	       "  -h          Show application help\n"
#ifdef _DEBUG
	       "  -t          Enable trace logs\n"
#endif
	       "  -v          Print application version and exit\n",
	      progname);
}

int main(int argc, char **argv)
{
	const char *cfg_file_path = NULL;
	basileus_t *bhnd;
	int opt;

#ifndef _DEBUG
#define _GETOPT_OPTSTR "c:nhv"
#else
#define _GETOPT_OPTSTR "c:nhtv"
#endif

	logger_init();

	while ((opt = getopt(argc, argv, _GETOPT_OPTSTR)) != -1) {
		switch (opt) {
		case 'c':
			cfg_file_path = optarg;
			if (access(cfg_file_path, R_OK) != 0) {
				log_error("Specified configuration file does not exist or is not readable!");
				return 1;
			}
			break;

		case 'n':
			logger_use_color = 0;
			break;

		case 'v':
			print_version();
			return 0;

#ifdef _DEBUG
		case 't':
			logger_show_trace = 1;
			break;
#endif

		case 'h':
		default:
			print_help(argv[0]);
			return 0;
		}
	}

	if ((bhnd = basileus_init(cfg_file_path)) == NULL) {
		log_error("Failed to start basileus!");
		return 1;
	}

	(void)basileus_run(bhnd);

	log_info("Terminating basileus...");
	basileus_shutdown(bhnd);
	log_info("Shutdown complete");

	return 0;
}
