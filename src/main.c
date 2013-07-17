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
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <libdaemon/dlog.h>
#include <libdaemon/dpid.h>
#include <libdaemon/dexec.h>
#include <libdaemon/dfork.h>
#include <libdaemon/dsignal.h>

#include "config.h"
#include "music_db.h"
#include "webserver.h"
#include "configuration.h"

typedef struct {
	cfg_t           *config;
	music_db_t      *music_db;
	webserver_t	*webserver;
} basileus_t;

static int
basileus_init(basileus_t *app, const char *config_path)
{
	if (!config_path) {
		config_path = DEFAULT_CONFIG_FILE_PATH;
	}

	if (access(config_path, R_OK) != 0) {
		daemon_log(LOG_ERR, "Failed to open configuration file: %s\n", config_path);
		return 1;
	}

	if ((app->config = configuration_init(config_path)) == NULL) {
		return 2;
	}

	if ((app->music_db = music_db_init(app->config)) == NULL) {
		return 3;
	}

	if ((app->webserver = webserver_init(app->config, app->music_db)) == NULL) {
		return 4;
	}

	if (music_db_refresh(app->music_db)) {
		return 5;
	}

	return 0;
}

static void
basileus_shutdown(basileus_t *app)
{
	if (app->webserver) {
		webserver_shutdown(app->webserver);
		app->webserver = NULL;
	}
	if (app->music_db) {
		music_db_shutdown(app->music_db);
		app->music_db = NULL;
	}
	if (app->config) {
		configuration_free(app->config);
		app->config = NULL;
	}
}

static void
print_help(const char *progname)
{
	printf("Usage: %s [options]\n"
	       "  Available options:\n"
	       "  -k          Kill already running daemon process\n"
	       "  -c <file>   Read program configuration from specified file\n"
	       "  -f          Start process in foreground\n"
	       "  -h          Show application help\n"
	       "  -v          Print application version and exit\n",
	      progname);
}

static void
print_version()
{
	printf("Basileus version %d.%d\n", BASILEUS_VERSION_MAJOR, BASILEUS_VERSION_MINOR);
}

int
main(int argc, char *argv[])
{
	const char *cfg_file_path = NULL;
	basileus_t application;
	int daemonize = 1;
	pid_t pid = 0;
	int opt;

	memset(&application, 0, sizeof(application));

	daemon_pid_file_ident = daemon_log_ident = daemon_ident_from_argv0(argv[0]);

	while ((opt = getopt(argc, argv, "hkvfc:")) != -1) {
		switch (opt) {
		case 'k':
			if (daemon_pid_file_kill_wait(SIGTERM, 5) < 0) {
				daemon_log(LOG_WARNING, "Failed to kill daemon: %s", strerror(errno));
				return 1;
			}
			return 0;
		case 'c':
			cfg_file_path = optarg;
			if (access(cfg_file_path, R_OK) != 0) {
				daemon_log(LOG_ERR, "Specified configuration file does not exist or is not readable!");
				return 1;
			}
			break;
		case 'v':
			print_version();
			return 0;
		case 'f':
			daemonize = 0;
			daemon_log_use = DAEMON_LOG_STDOUT;
			break;
		case 'h':
		default:
			print_help(argv[0]);
			return 0;
		}
	}

	if (daemon_reset_sigs(-1) < 0) {
		daemon_log(LOG_ERR, "Failed to reset all signal handlers: %s", strerror(errno));
		return 1;
	}

	if (daemon_unblock_sigs(-1) < 0) {
		daemon_log(LOG_ERR, "Failed to unblock all signals: %s", strerror(errno));
		return 1;
	}

	if ((pid = daemon_pid_file_is_running()) >= 0) {
		daemon_log(LOG_ERR, "Daemon already running on PID file %u", pid);
		return 1;
	}

	if (daemon_retval_init() < 0) {
		daemon_log(LOG_ERR, "Failed to create pipe.");
		return 1;
	}

	if (daemonize) {
		pid = daemon_fork();
	} else {
		pid = 0;
	}

	if (pid < 0) {
		daemon_retval_done();
		daemon_log(LOG_ERR, "Failed to fork new daemon process: %s\n", strerror(errno));
		return 1;
	} else if (pid) { /* parent */
		int ret;

		if ((ret = daemon_retval_wait(20)) < 0) {
			daemon_log(LOG_ERR, "Could not recieve return value from daemon process: %s", strerror(errno));
			return 255;
		}

		switch (ret) {
		case 0:
			daemon_log(LOG_INFO, "Daemon started succesfully.");
			break;
		case 1:
			daemon_log(LOG_ERR, "Daemon startup failed, could not close all file descriptors!");
			break;
		case 2:
			daemon_log(LOG_ERR, "Daemon startup failed, could not create pid file!");
			break;
		case 3:
			daemon_log(LOG_ERR, "Daemon startup failed, could not register signal handlers!");
			break;
		case 4:
			daemon_log(LOG_ERR, "Daemon startup failed, could not initialize basileus core!");
			break;
		default:
			daemon_log(LOG_ERR, "Daemon startup failed, unknown error code: %d\n", ret);
			break;
		}
		return ret;
	} else { /* daemon */
		int fd, quit = 0;
		fd_set fds;

		if (daemonize && daemon_close_all(-1) < 0) {
			daemon_log(LOG_ERR, "Failed to close all file descriptors: %s", strerror(errno));
			daemon_retval_send(1);
			goto finish;
		}

		if (daemonize && daemon_pid_file_create() < 0) {
			daemon_log(LOG_ERR, "Could not create PID file (%s).", strerror(errno));
			daemon_retval_send(2);
			goto finish;
		}

		if (daemon_signal_init(SIGINT, SIGTERM, SIGQUIT, SIGHUP, 0) < 0) {
			daemon_log(LOG_ERR, "Could not register signal handlers (%s).", strerror(errno));
			daemon_retval_send(3);
			goto finish;
		}

		if (basileus_init(&application, cfg_file_path) != 0) {
			daemon_log(LOG_ERR, "Failed to initialize application core!");
			daemon_retval_send(4);
			goto finish;
		}

		if (daemonize)
			daemon_retval_send(0);

		daemon_log(LOG_INFO, "Sucessfully started");

		FD_ZERO(&fds);
		fd = daemon_signal_fd();
		FD_SET(fd, &fds);

		while (!quit) {
			fd_set fds2 = fds;

			if (select(FD_SETSIZE, &fds2, 0, 0, 0) < 0) {

				if (errno == EINTR)
					continue;

				daemon_log(LOG_ERR, "select(): %s", strerror(errno));
				break;
			}

			if (FD_ISSET(fd, &fds2)) {
				int sig;

				if ((sig = daemon_signal_next()) <= 0) {
					daemon_log(LOG_ERR, "daemon_signal_next() failed: %s", strerror(errno));
					break;
				}

				/* Dispatch signal */
				switch (sig) {
				case SIGINT:
				case SIGQUIT:
				case SIGTERM:
					quit = 1;
					break;
				case SIGHUP:
					(void)music_db_refresh(application.music_db);
					break;
				default:
#ifdef _DEBUG
					daemon_log(LOG_WARNING, "Unhandled signal: %d", sig);
#endif
					break;
				}
			}
		}
finish:
		daemon_log(LOG_INFO, "Exiting...");
		basileus_shutdown(&application);
		daemon_retval_send(255);
		daemon_signal_done();
		daemon_pid_file_remove();

		return 0;
	}
}
