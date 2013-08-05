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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <limits.h>

#include <event2/event.h>
#include <event2/thread.h>

#include "scheduler.h"
#include "logger.h"
#include "cfg.h"

/*
 * Fake configuration
 */
const char*
cfg_get_str(cfg_t *cfg, cfg_key_t key)
{
	assert (cfg == NULL);

	if (key == CFG_SCHEDULER_THREADS) {
		return "0";
	} else {
		assert(0);
		return "";
	}
}

static void
_sighandler(evutil_socket_t signal, short events, void *user_data)
{
	struct event_base *evb = user_data;
	event_base_loopexit(evb, NULL);
}

struct task_data {
	int task_no;
	int cnt;
	int cancel;
};

static task_status_t
_task_run(void *data)
{
	struct task_data *td = data;

	if (td->cancel) {
		return TASK_STATUS_CANCELED;
	}

	if (td->cnt < 1000000) {
		td->cnt += 10;
		return TASK_STATUS_YIELD;
	} else {
		return TASK_STATUS_FINISHED;
	}

}

static void
_task_finished(void *user_data)
{
	struct task_data *td = user_data;
	log_info("Task %d finished", td->task_no);
	free(td);
}

static void
_task_failed(void *user_data)
{
	struct task_data *td = user_data;
	log_info("Task %d failed", td->task_no);
	free(td);
}

static void
_task_cancel(void *user_data)
{
	struct task_data *td = user_data;
	log_info("Task %d canceled!", td->task_no);
	td->cancel = 1;
	free(td);
}

static void
event_run(void *data)
{
	scheduler_t *sched = data;
	struct task_data *td;
	task_t *t;
	int i = 0;

#define TEST_TASKS 24

	for (i = 0; i < TEST_TASKS; i++) {
		t = malloc(sizeof(task_t));
		td = malloc(sizeof(struct task_data));
		assert(t && td);

		td->cancel = 0;
		td->task_no = i;
		td->cnt = 0;

		t->name = "Test task";
		t->user_data = td;
		t->run = _task_run;
		t->finished = _task_finished;
		t->failed = _task_failed;
		t->cancel = _task_cancel;
		scheduler_add_task(sched, t);
	}
}

int
main(int argc, char *argv[])
{
	scheduler_t *sched = NULL;
	struct event *sigevent = NULL;
	struct event_base *evb;
	int ret = 0;

	logger_show_trace = 0;

	event_enable_debug_mode();
	evb = event_base_new();
	if (!evb) {
		log_error("Failed to create event_base!");
		goto error;
	}
	if (0 != evthread_use_pthreads()) {
		log_error("Could not enable libevent thread safety!");
		goto error;
	}
	if (0 != evthread_make_base_notifiable(evb)) {
		log_error("Failed to make event base notifiable!");
		goto error;
	}
	evthread_enable_lock_debuging();

	sigevent = evsignal_new(evb, SIGINT, _sighandler, evb);
	if (!sigevent || event_add(sigevent, NULL) < 0) {
		log_error("Failed to register SIGINT handler!");
		goto error;
	}

	sched = scheduler_new(NULL, evb);
	if (!sched) {
		log_error("Failed to initialize scheduler");
		goto error;
	}

	event_t * ev = malloc(sizeof(event_t));
	if (!ev) {
		log_error("Failed to allocate memory for event!");
		goto error;
	}
	ev->name = "Main event";
	ev->user_data = sched;
	ev->run = event_run;

	scheduler_add_event(sched, ev);

	(void)event_base_dispatch(evb);

	goto done;

error:
	ret = -1;
done:
	if (sched) {
		scheduler_free(sched);
	}
	if (sigevent) {
		event_free(sigevent);
	}
	if (evb) {
		event_base_free(evb);
	}
	return ret;
}

