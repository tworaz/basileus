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
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "config.h"
#include "logger.h"
#include "basileus.h"
#include "music_db.h"
#include "webserver.h"
#include "configuration.h"

typedef struct {
	cfg_t           *config;
	music_db_t      *music_db;
	webserver_t	    *webserver;

#define _SOCK_READ 0
#define _SOCK_WRITE 1
	int		         sockfds[2];
} _basileus_t;

static int mainloop_fd = -1;

static void
_basileus_sighandler(int signal)
{
	log_debug("Got %s signal", strsignal(signal));

	if (signal == SIGUSR1) {
		basileus_trigger_action(REFRESH_MUSIC_DB);
	} else if (signal == SIGINT || signal == SIGTERM) {
		basileus_trigger_action(TERMINATE);
	} else {
		assert(0);
	}
}

basileus_t
basileus_init(const char *config_path)
{
	_basileus_t *app = NULL;
	struct sigaction sa;
	sigset_t sigset;

	if (!config_path) {
		config_path = DEFAULT_CONFIG_FILE_PATH;
	}

	sigemptyset(&sigset);
	if (0 != sigaddset(&sigset, SIGINT)) {
		log_error("Failed to add SIGINT to signal set!");
		return NULL;
	}
	if (0 != sigaddset(&sigset, SIGTERM)) {
		log_error("Failed to add SIGTERM to signal set!");
		return NULL;
	}
	if (0 != sigaddset(&sigset, SIGUSR1)) {
		log_error("Failed to add SIGUSR1 to signal set!");
		return NULL;
	}

	sa.sa_handler = _basileus_sighandler;
	sa.sa_mask = sigset;
	if (-1 == sigaction(SIGINT, &sa, NULL)) {
		log_error("Failed to register SIGINT handler: %s", strerror(errno));
		return NULL;
	}
	if (-1 == sigaction(SIGTERM, &sa, NULL)) {
		log_error("Failed to register SIGTERM handler: %s", strerror(errno));
		return NULL;
	}
	if (-1 == sigaction(SIGUSR1, &sa, NULL)) {
		log_error("Failed to register SIGUSR1 handler: %s", strerror(errno));
		return NULL;
	}

	app = malloc(sizeof(_basileus_t));
	if (app == NULL) {
		log_error("Failed to allocate memory for basileus core!");
		goto failure;
	}
	app->sockfds[0] = -1;
	app->sockfds[1] = -1;

	if (-1 == socketpair(AF_UNIX, SOCK_DGRAM, 0, app->sockfds)) {
		log_error("Failed to create socket pair: %s", strerror(errno));
		goto failure;
	}
	mainloop_fd = app->sockfds[_SOCK_WRITE];

	if (access(config_path, R_OK) != 0) {
		log_error("Failed to open configuration file: %s\n", config_path);
		goto failure;
	}

	if ((app->config = configuration_init(config_path)) == NULL) {
		goto failure;
	}

	if ((app->music_db = music_db_init(app->config)) == NULL) {
		goto failure;
	}

	if ((app->webserver = webserver_init(app->config, app->music_db)) == NULL) {
		goto failure;
	}

	if (music_db_refresh(app->music_db)) {
		goto failure;
	}

	log_info("Basileus %d.%d started", BASILEUS_VERSION_MAJOR, BASILEUS_VERSION_MINOR);

	return app;

failure:
	if (app != NULL) {
		basileus_shutdown(app);
	}
	return NULL;
}

void
basileus_shutdown(basileus_t basileus)
{
	_basileus_t *app = basileus;

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
	if (app->sockfds[0]) {
		close(app->sockfds[0]);
	}
	if (app->sockfds[1]) {
		close(app->sockfds[1]);
	}
	free(app);
}

int
basileus_run(basileus_t basileus)
{
	_basileus_t *app = basileus;
	basileus_action_t action;
	ssize_t size;

	while (1) {
		size = recv(app->sockfds[_SOCK_READ], &action, sizeof(action), MSG_WAITALL);
		if (size == -1) {
			if (errno == EINTR) {
				continue;
			}
			log_error("Failed to receive mainloop message (%d)!", errno);
			return errno;
		}
		assert(size == sizeof(action));

		switch (action)
		{
		case TERMINATE:
			log_debug("Got terminate action, exiting main loop.");
			return 0;

		case REFRESH_MUSIC_DB:
			log_debug("Got music db refresh request.");
			(void)music_db_refresh(app->music_db);
			break;

		default:
			log_error("Unknown action: %d", action);
			break;
		}
	}

	return 0;
}

void
basileus_trigger_action(basileus_action_t action)
{
	ssize_t w;

	assert(mainloop_fd != -1);

retry:
	w = send(mainloop_fd, &action, sizeof(action), 0);
	if (w < 0) {
		if (errno == EINTR) {
			goto retry;
		}
		log_error("Failed to send message to application main loop (%d)!", errno);
		exit(-1);
	}
	assert(w == sizeof(action));
}
