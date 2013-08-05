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

#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include <event2/event.h>

#include "cfg.h"

typedef void scheduler_t;

typedef enum {
	TASK_STATUS_FINISHED = 0,
	TASK_STATUS_YIELD    = 1,
	TASK_STATUS_CANCELED = 2,
	TASK_STATUS_FAILED   = 3
} task_status_t;

typedef struct {
	const char     *name;
	void           *user_data;
	task_status_t  (*run)       (void *user_data);
	void           (*finished)  (void *user_data);
	void           (*failed)    (void *user_data);
	void           (*cancel)    (void *user_data);
} task_t;

typedef struct {
	const char     *name;
	void           *user_data;
	void           (*run)(void *user_data);
} event_t;

scheduler_t *
scheduler_new(cfg_t *config, struct event_base *evb);

void
scheduler_free(scheduler_t *);

int
scheduler_add_task(scheduler_t *sched, task_t *task);

int
scheduler_add_event(scheduler_t *sched, event_t *event);

#endif /* _SCHEDULRER_H_ */
