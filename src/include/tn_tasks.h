/*******************************************************************************
 *
 * TNKernel real-time kernel
 *
 * Copyright © 2011-2016 Sergey Koshkin <koshkin.sergey@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 ******************************************************************************/

#ifndef TN_TASKS_H_
#define TN_TASKS_H_

/*******************************************************************************
 *  includes
 ******************************************************************************/

#include "tn_arch.h"
#include "tn.h"

/*******************************************************************************
 *  defines and macros (scope: module-local)
 ******************************************************************************/

/*******************************************************************************
 *  typedefs and structures (scope: module-local)
 ******************************************************************************/

typedef struct RUN_TASK {
  TN_TCB *curr;                     //-- Task that is running now
  TN_TCB *next;                     //-- Task to be run after switch context
} *pRUN_TASK;

/*******************************************************************************
 *  exported variables
 ******************************************************************************/

extern struct RUN_TASK run_task;
extern unsigned int tn_ready_to_run_bmp;
extern CDLL_QUEUE tn_ready_list[TN_NUM_PRIORITY];

/*******************************************************************************
 *  exported function prototypes
 ******************************************************************************/
extern
bool task_wait_complete(TN_TCB *task);
extern
void task_to_wait_action(TN_TCB *task, CDLL_QUEUE *wait_que, int wait_reason,
                         unsigned long timeout);
extern
void change_running_task_priority(TN_TCB *task, int new_priority);
extern
void set_current_priority(TN_TCB *task, int priority);
extern
void task_wait_delete(CDLL_QUEUE *que);
extern
void task_exit(void);

#endif /* TN_TASKS_H_ */

/* ----------------------------- End of file ---------------------------------*/
