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

#include <event2/event.h>
#include <event2/thread.h>

#include "config.h"
#include "logger.h"
#include "basileus.h"
#include "scheduler.h"
#include "music_db.h"
#include "webserver.h"
#include "cfg.h"

typedef struct {
	cfg_t               *config;
	music_db_t          *music_db;
	webserver_t         *webserver;
	scheduler_t         *scheduler;

	struct event_base   *ev_base;
	struct event        *term_evt;
	struct event        *int_evt;
	struct event        *hup_evt;
	struct event        *usr1_evt;
} _basileus_t;

void
_rescan_task(void *data)
{
	_basileus_t *app = data;
	log_debug("Got music db refresh request.");
	(void)music_db_refresh(app->music_db);
}

static void
_basileus_sighandler(evutil_socket_t signal, short events, void *user_data)
{
	_basileus_t *app = user_data;

	if (signal == SIGINT || signal == SIGTERM || signal == SIGHUP) {
		log_debug("Got termination request, terminating main loop");
		event_base_loopexit(app->ev_base, NULL);
	} else if (signal == SIGUSR1) {
		event_t *e = malloc(sizeof(event_t));
		if (e == NULL) {
			log_error("Failed to allocate memory for rescan task!");
			return;
		}
		memset(e, 0, sizeof(event_t));

		e->name = "Music Database rescan";
		e->run = _rescan_task;
		e->user_data = user_data;
		if (0 != scheduler_add_event(app->scheduler, e)) {
			log_error("Failed to scheduler Music DB update task!");
			free(e);
			return;
		}
	} else {
		assert(0);
	}
}

basileus_t *
basileus_init(const char *config_path)
{
	_basileus_t *app = NULL;
	struct event_base *evb = NULL;

	if (!config_path) {
		config_path = DEFAULT_CONFIG_FILE_PATH;
	}

	app = malloc(sizeof(_basileus_t));
	if (app == NULL) {
		log_error("Failed to allocate memory for basileus core!");
		goto failure;
	}
	memset(app, 0, sizeof(_basileus_t));

#ifdef _DEBUG
	event_enable_debug_mode();
#endif /* _DEBUG */

	evb = app->ev_base = event_base_new();
	if (!app->ev_base) {
		log_error("Failed to create event_base!");
		goto failure;
	}
	if (0 != evthread_use_pthreads()) {
		log_error("Could not enable libevent thread safety!");
		goto failure;
	}
	if (0 != evthread_make_base_notifiable(evb)) {
		log_error("Failed to make event base notifiable!");
		goto failure;
	}

#ifdef _DEBUG
	evthread_enable_lock_debuging();
#endif /* _DEBUG */

	app->term_evt = evsignal_new(evb, SIGTERM, _basileus_sighandler, app);
	if (!app->term_evt || event_add(app->term_evt, NULL) < 0) {
		log_error("Failed to register SIGTERM handler!");
		goto failure;
	}
	app->int_evt = evsignal_new(evb, SIGINT, _basileus_sighandler, app);
	if (!app->int_evt || event_add(app->int_evt, NULL) < 0) {
		log_error("Failed to register SIGINT handler!");
		goto failure;
	}
	app->hup_evt = evsignal_new(evb, SIGHUP, _basileus_sighandler, app);
	if (!app->hup_evt || event_add(app->hup_evt, NULL) < 0) {
		log_error("Failed to register SIGHUP handler!");
		goto failure;
	}
	app->usr1_evt = evsignal_new(evb, SIGUSR1, _basileus_sighandler, app);
	if (!app->usr1_evt || event_add(app->usr1_evt, NULL) < 0) {
		log_error("Failed to register SIGUSR1 handler!");
		goto failure;
	}

	if (access(config_path, R_OK) != 0) {
		log_error("Failed to open configuration file: %s\n", config_path);
		goto failure;
	}
	if ((app->config = cfg_init(config_path)) == NULL) {
		goto failure;
	}
	if ((app->scheduler = scheduler_new(app->config, evb)) == NULL) {
		goto failure;
	}
	if ((app->music_db = music_db_new(app->config, app->scheduler)) == NULL) {
		goto failure;
	}
	if ((app->webserver = webserver_init(app->config, app->music_db, evb)) == NULL) {
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
basileus_shutdown(basileus_t *basileus)
{
	_basileus_t *app = basileus;

	if (app->webserver) {
		webserver_shutdown(app->webserver);
		app->webserver = NULL;
	}
	if (app->music_db) {
		music_db_free(app->music_db);
		app->music_db = NULL;
	}
	if (app->scheduler) {
		scheduler_free(app->scheduler);
	}
	if (app->config) {
		cfg_free(app->config);
		app->config = NULL;
	}
	if (app->term_evt) {
		event_free(app->term_evt);
	}
	if (app->int_evt) {
		event_free(app->int_evt);
	}
	if (app->hup_evt) {
		event_free(app->hup_evt);
	}
	if (app->usr1_evt) {
		event_free(app->usr1_evt);
	}
	if (app->ev_base) {
		event_base_free(app->ev_base);
	}
	free(app);
}

int
basileus_run(basileus_t *basileus)
{
	_basileus_t *app = basileus;

	log_info("Entering event dispatch loop...");
	int ret = event_base_dispatch(app->ev_base);
	log_info("Event dispatch loop terminated");

	return ret;
}
