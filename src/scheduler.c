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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/queue.h>

#include "logger.h"
#include "scheduler.h"

typedef struct event_queue_elm {
	SIMPLEQ_ENTRY(event_queue_elm) queue;
	event_t *event;
} event_queue_elm_t;

typedef struct task_queue_elm {
	SIMPLEQ_ENTRY(task_queue_elm) queue;
	task_t *task;
} task_queue_elm_t;

typedef struct {
	struct event_base *evb;
	struct event      *event;

	pthread_cond_t    cv;
	pthread_mutex_t   mutex;

	int               workers_count;
	pthread_t        *workers;

	SIMPLEQ_HEAD(,event_queue_elm)	event_queue;
	SIMPLEQ_HEAD(,task_queue_elm)   task_queue;

	int terminate;
} _scheduler_t;

static void
_execute_tasks(_scheduler_t *sched)
{
	task_queue_elm_t *elm = NULL;
	task_status_t status;
	task_t *task = NULL;

	while (1) {
		pthread_mutex_lock(&sched->mutex);

		if (SIMPLEQ_EMPTY(&sched->task_queue)) {
			log_trace("No more pending tasks");
			return;
		}

		elm = SIMPLEQ_FIRST(&sched->task_queue);
		SIMPLEQ_REMOVE_HEAD(&sched->task_queue, queue);

		pthread_mutex_unlock(&sched->mutex);

		task = elm->task;

		log_trace("Executing task: %s", task->name);
		status =  task->run(task->user_data);

		if (status == TASK_STATUS_FINISHED) {
			log_trace("Task finished: %s", task->name);
			task->finished(task->user_data);
		} else if (status == TASK_STATUS_FAILED) {
			log_trace("Task failed: %s", task->name);
			task->failed(task->user_data);
		} else if (status == TASK_STATUS_YIELD) {
			log_trace("Task yielded: %s", task->name);
			pthread_mutex_lock(&sched->mutex);
			SIMPLEQ_INSERT_TAIL(&sched->task_queue, elm, queue);
			pthread_mutex_unlock(&sched->mutex);
			continue;
		}

		free(elm);
		free(task);

		if (status == TASK_STATUS_CANCELED) {
			log_trace("Task canceled: %s", task->name);
			return;
		}
	}
}

static void *
_worker_thread(void *arg)
{
	_scheduler_t *ts = arg;
	int id = -1;

	pthread_mutex_lock(&ts->mutex);

	for (id = 0; id < ts->workers_count; id++) {
		pthread_t thr = pthread_self();
		if (pthread_equal(ts->workers[id], thr)) {
			++id;
			break;
		}
	}

	assert(id <= ts->workers_count);

	log_info("Scheduler: Worker thread %d started", id);

	while (!ts->terminate) {
		if (0 != pthread_cond_wait(&ts->cv, &ts->mutex)) {
			log_error("Condition variable wait failed!");
			pthread_exit((void **)-1);
		}
		log_trace("Scheduler thread %d woken up", id);
		if (ts->terminate) {
			break;
		}

		pthread_mutex_unlock(&ts->mutex);

		_execute_tasks(ts);
	}

	pthread_mutex_unlock(&ts->mutex);

	log_info("Scheduler: Worker thread %d exiting ...", id);

	pthread_exit(0);
}

static void
_event_handler(evutil_socket_t fd, short event, void *arg)
{
	_scheduler_t *sch = arg;
	event_queue_elm_t *elm = NULL;

	pthread_mutex_lock(&sch->mutex);

	while (!SIMPLEQ_EMPTY(&sch->event_queue)) {
		elm = SIMPLEQ_FIRST(&sch->event_queue);

		pthread_mutex_unlock(&sch->mutex);

		log_debug("Processing event: %s", elm->event->name);
		elm->event->run(elm->event->user_data);
		log_debug("Event processed: %s", elm->event->name);

		pthread_mutex_lock(&sch->mutex);

		SIMPLEQ_REMOVE_HEAD(&sch->event_queue, queue);

		free(elm->event);
		free(elm);
		elm = NULL;
	}

	pthread_mutex_unlock(&sch->mutex);
}

static int
_get_thread_count(cfg_t *cfg)
{
	int cnt;

	cnt = atoi(cfg_get_str(cfg, CFG_SCHEDULER_THREADS));
	if (cnt > 0) {
		return cnt;
	}

	cnt = sysconf(_SC_NPROCESSORS_ONLN);
	if (cnt <= 0) {
		log_warning("Could not determine number of CPUs, assuming 1");
		return 1;
	} else if (cnt > 1) {
		/* Leave one core for the main loop */
		return cnt - 1;
	}

	return 1;
}

scheduler_t *
scheduler_new(cfg_t *config, struct event_base *evb)
{
	_scheduler_t *ts = NULL;
	int i;

	ts = malloc(sizeof(_scheduler_t));
	if (NULL == ts) {
		log_error("Failed to allocate memory for task scheduler!");
		return NULL;
	}
	memset(ts, 0, sizeof(_scheduler_t));

	SIMPLEQ_INIT(&ts->event_queue);
	SIMPLEQ_INIT(&ts->task_queue);

	ts->event = event_new(evb, -1, 0, _event_handler, ts);
	if (NULL == ts->event) {
		log_error("Failed to create new scheduler event!");
		goto error;
	}

	if (0 != pthread_mutex_init(&ts->mutex, NULL)) {
		log_error("Failed to initialize task scheduler mutex!");
		free(ts);
		return NULL;
	}

	if (0 != pthread_cond_init(&ts->cv, NULL)) {
		log_error("Failed to initialize scheduler condition variable!");
		pthread_mutex_destroy(&ts->mutex);
		free(ts);
		return NULL;
	}

	ts->workers_count = _get_thread_count(config);
	log_info("Scheduler: found %d CPUs", ts->workers_count);

	ts->workers = malloc(ts->workers_count * sizeof(pthread_t));
	if (NULL == ts->workers) {
		log_error("Failed to allocate memory for thread storage!");
		goto error;
	}
	memset(ts->workers, 0, ts->workers_count * sizeof(pthread_t));

	ts->terminate = 0;

	pthread_mutex_lock(&ts->mutex);

	for (i = 0; i < ts->workers_count; i++) {
		if (0 != pthread_create(&ts->workers[i], NULL, _worker_thread, ts)) {
			log_error("Failed to initalize worker thread!");
			pthread_mutex_unlock(&ts->mutex);
			goto error;
		}
	}

	pthread_mutex_unlock(&ts->mutex);

	ts->evb = evb;

	return ts;

error:
	if (ts->workers) {
		free(ts->workers);
	}
	pthread_mutex_destroy(&ts->mutex);
	pthread_cond_destroy(&ts->cv);
	free(ts);
	return NULL;
};

void
scheduler_free(scheduler_t *sch)
{
	_scheduler_t *ts = sch;
	task_queue_elm_t *telm, *tnx;
	event_queue_elm_t *eelm, *enx;
	int i;

	log_info("Scheduler: Stopping threads");

	pthread_mutex_lock(&ts->mutex);
	ts->terminate = 1;
	SIMPLEQ_FOREACH(telm, &ts->task_queue, queue) {
		if (telm->task->cancel) {
			telm->task->cancel(telm->task->user_data);
		}
	}
	pthread_mutex_unlock(&ts->mutex);

	pthread_cond_broadcast(&ts->cv);

	for (i = 0; i < ts->workers_count; i++) {
		pthread_join(ts->workers[i], NULL);
	}

	log_info("Scheduler: Threads stopped");

	event_del(ts->event);
	event_free(ts->event);

	tnx = telm = SIMPLEQ_FIRST(&ts->task_queue);
	while (tnx) {
		tnx = SIMPLEQ_NEXT(telm, queue);
		free(telm->task);
		free(telm);
		telm = tnx;
	}

	enx = eelm = SIMPLEQ_FIRST(&ts->event_queue);
	while (enx) {
		enx = SIMPLEQ_NEXT(eelm, queue);
		free(eelm->event);
		free(eelm);
		eelm = enx;
	}

	if (ts->workers) {
		free(ts->workers);
	}
	pthread_cond_destroy(&ts->cv);
	pthread_mutex_destroy(&ts->mutex);
	free(ts);
}

int
scheduler_add_task(scheduler_t *s, task_t *task)
{
	_scheduler_t *sched = s;
	task_queue_elm_t *elm = NULL;
	int ret = 0;

	log_debug("Scheduler: Adding new task: %s", task->name);

	elm  = malloc(sizeof(task_queue_elm_t));
	if (elm == NULL) {
		log_error("Failed to allocate memory for queue element!");
		return -1;
	}
	elm->task = task;

	pthread_mutex_lock(&sched->mutex);

	SIMPLEQ_INSERT_TAIL(&sched->task_queue, elm, queue);

	pthread_mutex_unlock(&sched->mutex);

	log_trace("Scheduler: Waking up worker thread");
	pthread_cond_signal(&sched->cv);

	return ret;
}

int
scheduler_add_event(scheduler_t *s, event_t *event)
{
	_scheduler_t *sched = s;
	event_queue_elm_t *elm = NULL;
	int ret = 0;

	log_debug("Scheduler: Adding new event: %s", event->name);

	elm  = malloc(sizeof(event_queue_elm_t));
	if (elm == NULL) {
		log_error("Failed to allocate memory for queue element!");
		return -1;
	}
	elm->event = event;

	pthread_mutex_lock(&sched->mutex);

	SIMPLEQ_INSERT_TAIL(&sched->event_queue, elm, queue);

	struct timeval tv = {0, 0};
	if (0 != event_add(sched->event, &tv)) {
		log_error("Failed to schedule event!");
		SIMPLEQ_REMOVE(&sched->event_queue, elm, event_queue_elm, queue);
		free(elm);
		ret = -1;
	}

	pthread_mutex_unlock(&sched->mutex);

	return ret;
}
