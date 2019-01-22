/*
 * Copyright (C) 2017-2019 Sergey Koshkin <koshkin.sergey@gmail.com>
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Project: uKernel real-time kernel
 */

/**
 * @file
 *
 * Kernel system routines.
 *
 */

/*******************************************************************************
 *  includes
 ******************************************************************************/

#include "knl_lib.h"

/*******************************************************************************
 *  external declarations
 ******************************************************************************/

/*******************************************************************************
 *  defines and macros (scope: module-local)
 ******************************************************************************/

/*******************************************************************************
 *  typedefs and structures (scope: module-local)
 ******************************************************************************/

/*******************************************************************************
 *  global variable definitions  (scope: module-exported)
 ******************************************************************************/

/*******************************************************************************
 *  global variable definitions (scope: module-local)
 ******************************************************************************/

/*******************************************************************************
 *  function implementations (scope: module-local)
 ******************************************************************************/

static
void ThreadStackInit(uint32_t func_addr, void *func_param, osThread_t *thread)
{
  uint32_t *stk = (uint32_t *)((uint32_t)thread->stk_mem + thread->stk_size);

  *(--stk) = 0x01000000L;                       //-- xPSR
  *(--stk) = func_addr;                         //-- Entry Point
  *(--stk) = (uint32_t)osThreadExit;            //-- R14 (LR)
  *(--stk) = 0x12121212L;                       //-- R12
  *(--stk) = 0x03030303L;                       //-- R3
  *(--stk) = 0x02020202L;                       //-- R2
  *(--stk) = 0x01010101L;                       //-- R1
  *(--stk) = (uint32_t)func_param;              //-- R0 - task's function argument
  *(--stk) = 0x11111111L;                       //-- R11
  *(--stk) = 0x10101010L;                       //-- R10
  *(--stk) = 0x09090909L;                       //-- R9
  *(--stk) = 0x08080808L;                       //-- R8
  *(--stk) = 0x07070707L;                       //-- R7
  *(--stk) = 0x06060606L;                       //-- R6
  *(--stk) = 0x05050505L;                       //-- R5
  *(--stk) = 0x04040404L;                       //-- R4

  thread->stk = (uint32_t)stk;
}

static osThread_t *ThreadHighestPrioGet(void)
{
  uint8_t priority;
  osThread_t *thread;

  priority = (NUM_PRIORITY - 1U) - __CLZ(knlInfo.ready_to_run_bmp);
  thread = GetTaskByQueue(knlInfo.ready_list[priority].next);

  return (thread);
}

static void ThreadSwitch(osThread_t *thread)
{
  thread->state = ThreadStateRunning;
  knlInfo.run.next = thread;
  archSwitchContextRequest();
}

/**
 * @brief       Dispatch specified Thread or Ready Thread with Highest Priority.
 * @param[in]   thread  thread object or NULL.
 */
static void ThreadDispatch(osThread_t *thread)
{
  osThread_t *thread_running;

  if (thread == NULL) {
    thread = ThreadHighestPrioGet();
  }

  thread_running = ThreadGetRunning();

  if (thread->priority > thread_running->priority) {
    thread_running->state = ThreadStateReady;
    ThreadSwitch(thread);
  }
}

/**
 * @brief       Adds thread to the end of ready queue for current priority
 * @param[in]   thread
 */
static void ThreadReadyAdd(osThread_t *thread)
{
  knlInfo_t *info = &knlInfo;
  int8_t priority = thread->priority - 1U;

  /* Remove the thread from any queue */
  QueueRemoveEntry(&thread->task_que);

  thread->state = ThreadStateReady;
  /* Add the thread to the end of ready queue */
  QueueAddTail(&info->ready_list[priority], &thread->task_que);
  info->ready_to_run_bmp |= (1UL << priority);
}

/**
 * @brief       Deletes thread from the ready queue for current priority
 * @param[in]   thread
 */
static void ThreadReadyDel(osThread_t *thread)
{
  knlInfo_t *info = &knlInfo;
  int8_t priority = thread->priority - 1U;
  queue_t *que = &info->ready_list[priority];

  /* Remove the thread from ready queue */
  QueueRemoveEntry(&thread->task_que);

  if (isQueueEmpty(que)) {
    /* No ready threads for the current priority */
    info->ready_to_run_bmp &= ~(1UL << priority);
  }
}

/**
 * @brief
 * @param[out]  task
 */
static
void ThreadWaitExit_Handler(osThread_t *task)
{
  BEGIN_CRITICAL_SECTION

  _ThreadWaitExit(task, (uint32_t)TERR_TIMEOUT);

  END_CRITICAL_SECTION
}

osThreadId_t ThreadNew(uint32_t func_addr, void *argument, const osThreadAttr_t *attr)
{
  osThread_t   *thread;
  void         *stack_mem;
  uint32_t      stack_size;
  osPriority_t  priority;

  if ((func_addr == 0U) || (attr == NULL)) {
    return (NULL);
  }

  thread = attr->cb_mem;
  stack_mem = attr->stack_mem;
  stack_size = attr->stack_size;
  priority = attr->priority;

  if ((thread == NULL) || (attr->cb_size < sizeof(osThread_t))) {
    return (NULL);
  }

  if ((stack_mem == NULL) || (((uint32_t)stack_mem & 7U) != 0U) ||
      (stack_size < 64U)  || ((stack_size & 7U) != 0U)            ) {
    return (NULL);
  }

  if (priority == osPriorityNone) {
    priority = osPriorityNormal;
  }
  else if ((priority < osPriorityIdle) || priority > osPriorityISR) {
    return (NULL);
  }

  /* Init thread control block */
  thread->stk_mem = stack_mem;
  thread->stk_size = stack_size;
  thread->base_priority = (int8_t)priority;
  thread->priority = (int8_t)priority;
  thread->name = attr->name;
  thread->id = ID_THREAD;
  thread->tslice_count = 0;

  QueueReset(&thread->task_que);
  QueueReset(&thread->wait_timer.timer_que);
  QueueReset(&thread->mutex_que);

  /* Fill all task stack space by FILL_STACK_VAL */
  uint32_t *ptr = stack_mem;
  for (uint32_t i = stack_size/sizeof(uint32_t); i != 0U; --i) {
    *ptr++ = FILL_STACK_VALUE;
  }

  ThreadStackInit(func_addr, argument, thread);

  ThreadReadyAdd(thread);
  ThreadDispatch(thread);

  return (thread);
}

static
const char *ThreadGetName(osThreadId_t thread_id)
{
  osThread_t *thread = (osThread_t *)thread_id;

  /* Check parameters */
  if ((thread == NULL) || (thread->id != ID_THREAD)) {
    return (NULL);
  }

  return (thread->name);
}

static
osThreadId_t ThreadGetId(void)
{
  osThread_t *thread;

  thread = ThreadGetRunning();

  return (thread);
}

static
osThreadState_t ThreadGetState(osThreadId_t thread_id)
{
  osThread_t *thread = (osThread_t *)thread_id;

  /* Check parameters */
  if ((thread == NULL) || (thread->id != ID_THREAD)) {
    return (osThreadError);
  }

  return (thread->state);
}

static
uint32_t ThreadGetStackSize(osThreadId_t thread_id)
{
  osThread_t *thread = (osThread_t *)thread_id;

  /* Check parameters */
  if ((thread == NULL) || (thread->id != ID_THREAD)) {
    return (0U);
  }

  return (thread->stk_size);
}

static
uint32_t ThreadGetStackSpace(osThreadId_t thread_id)
{
  osThread_t *thread = (osThread_t *)thread_id;
  const uint32_t *stack;
  uint32_t space = 0U;

  /* Check parameters */
  if ((thread == NULL) || (thread->id != ID_THREAD)) {
    return (0U);
  }

  stack = thread->stk_mem;
  for (; space < thread->stk_size; space += sizeof(uint32_t)) {
    if (*stack++ != FILL_STACK_VALUE) {
      break;
    }
  }

  return (space);
}

static osStatus_t ThreadSetPriority(osThreadId_t thread_id, osPriority_t priority)
{
  osThread_t *thread = (osThread_t *)thread_id;

  /* Check parameters */
  if ((thread == NULL) || (thread->id != ID_THREAD) ||
      (priority < osPriorityIdle) || (priority > osPriorityISR)) {
    return (osErrorParameter);
  }

  /* Check object state */
  if (thread->state == ThreadStateTerminated) {
    return (osErrorResource);
  }

  _ThreadSetPriority(thread, (int8_t)priority);

  return (osOK);
}

static osPriority_t ThreadGetPriority(osThreadId_t thread_id)
{
  osThread_t *thread = (osThread_t *)thread_id;
  osPriority_t priority;

  /* Check parameters */
  if ((thread == NULL) || (thread->id != ID_THREAD)) {
    return (osPriorityError);
  }

  /* Check object state */
  if (thread->state == ThreadStateTerminated) {
    return (osPriorityError);
  }

  priority = (osPriority_t)thread->priority;

  return (priority);
}

static osStatus_t ThreadYield(void)
{
  int8_t priority;
  queue_t *que;
  osThread_t *thread_running;
  osThread_t *thread_ready;
  knlInfo_t *info = &knlInfo;

  thread_running = ThreadGetRunning();
  priority = thread_running->priority - 1U;
  que = &info->ready_list[priority];

  /* Remove the running thread from ready queue */
  QueueRemoveEntry(&thread_running->task_que);

  if (!isQueueEmpty(que)) {
    thread_running->state = ThreadStateReady;
    thread_ready = GetTaskByQueue(info->ready_list[priority].next);
    ThreadSwitch(thread_ready);
  }

  /* Add the running thread to the end of ready queue */
  QueueAddTail(&info->ready_list[priority], &thread_running->task_que);

  return (osOK);
}

static
osStatus_t ThreadSuspend(osThreadId_t thread_id)
{
  osThread_t *thread = (osThread_t *)thread_id;
  osStatus_t status = osOK;

  /* Check parameters */
  if ((thread == NULL) || (thread->id != ID_THREAD)) {
    return (osErrorParameter);
  }

  switch (thread->state) {
    case ThreadStateRunning:
      ThreadReadyDel(thread);
      thread->state = ThreadStateBlocked;
      thread = ThreadHighestPrioGet();
      ThreadSwitch(thread);
      break;

    case ThreadStateReady:
      ThreadReadyDel(thread);
      thread->state = ThreadStateBlocked;
      break;

    case ThreadStateBlocked:
      /* Remove the thread from timer queue */
      QueueRemoveEntry(&thread->wait_timer.timer_que);
      /* Remove the thread from wait queue */
      QueueRemoveEntry(&thread->task_que);
      break;

    case ThreadStateTerminated:
    case ThreadStateInactive:
    default:
      status = osErrorResource;
      break;
  }

  return (status);
}

static osStatus_t ThreadResume(osThreadId_t thread_id)
{
  osThread_t *thread = (osThread_t *)thread_id;

  /* Check parameters */
  if ((thread == NULL) || (thread->id != ID_THREAD)) {
    return (osErrorParameter);
  }

  /* Check object state */
  if (thread->state != ThreadStateBlocked) {
    return (osErrorResource);
  }

  /* Remove the thread from timer queue */
  QueueRemoveEntry(&thread->wait_timer.timer_que);
  ThreadReadyAdd(thread);
  ThreadDispatch(thread);

  return (osOK);
}

static void ThreadExit(void)
{
  osThread_t *thread = ThreadGetRunning();

  /* Release owned Mutexes */
  MutexOwnerRelease(&thread->mutex_que);

  ThreadReadyDel(thread);
  ThreadSwitch(ThreadHighestPrioGet());
  thread->state = ThreadStateInactive;
  thread->id = ID_INVALID;
}

static osStatus_t ThreadTerminate(osThreadId_t thread_id)
{
  osThread_t *thread = (osThread_t *)thread_id;
  osStatus_t status = osOK;

  /* Check parameters */
  if ((thread == NULL) || (thread->id != ID_THREAD)) {
    return (osErrorParameter);
  }

  /* Check object state */
  switch (thread->state) {
    case ThreadStateRunning:
    case ThreadStateReady:
      ThreadReadyDel(thread);
      break;

    case ThreadStateBlocked:
      /* Remove the thread from timer queue */
      QueueRemoveEntry(&thread->wait_timer.timer_que);
      /* Remove the thread from wait queue */
      QueueRemoveEntry(&thread->task_que);
      break;

    case ThreadStateInactive:
    case ThreadStateTerminated:
    default:
      status = osErrorResource;
      break;
  }

  if (status == osOK) {
    /* Release owned Mutexes */
    MutexOwnerRelease(&thread->mutex_que);

    if (thread->state == ThreadStateRunning) {
      ThreadSwitch(ThreadHighestPrioGet());
    }
    else {
      ThreadDispatch(NULL);
    }

    thread->state = ThreadStateInactive;
    thread->id = ID_INVALID;
  }

  return (status);
}

static uint32_t ThreadGetCount(void)
{
  return (0U);
}

static uint32_t ThreadEnumerate(osThreadId_t *thread_array, uint32_t array_items)
{
  return (0U);
}

/*******************************************************************************
 *  Library functions
 ******************************************************************************/

/**
 * @brief       Exit Thread wait state.
 * @param[out]  thread    thread object.
 * @param[in]   ret_val   return value.
 */
void _ThreadWaitExit(osThread_t *thread, uint32_t ret_val)
{
  thread->wait_info.ret_val = ret_val;

  QueueRemoveEntry(&thread->wait_timer.timer_que);
  ThreadReadyAdd(thread);
  ThreadDispatch(thread);
}

/**
 * @brief
 * @param thread
 * @param wait_que
 * @param timeout
 */
void _ThreadWaitEnter(osThread_t *thread, queue_t *wait_que, uint32_t timeout)
{
  ThreadReadyDel(thread);

  thread->state = ThreadStateBlocked;

  /* Add to the wait queue - FIFO */
  if (wait_que != NULL) {
    QueueAddTail(wait_que, &thread->task_que);
  }

  /* Add to the timers queue */
  if (timeout != osWaitForever)
    TimerInsert(&thread->wait_timer, knlInfo.jiffies + timeout, (CBACK)ThreadWaitExit_Handler, thread);

  thread = ThreadHighestPrioGet();
  ThreadSwitch(thread);
}

/**
 * @brief
 * @param wait_que
 */
void _ThreadWaitDelete(queue_t *wait_que)
{
  while (!isQueueEmpty(wait_que)) {
    _ThreadWaitExit(GetTaskByQueue(QueueRemoveHead(wait_que)), (uint32_t)TERR_DLT);
  }
}

/**
 * @brief       Change priority of a thread.
 * @param[in]   thread    thread object.
 * @param[in]   priority  new priority value for the thread.
 */
void _ThreadSetPriority(osThread_t *thread, int8_t priority)
{
  if (thread->priority != priority) {
    thread->base_priority = priority;

    if (thread->state == ThreadStateBlocked) {
      thread->priority = priority;
    }
    else {
      ThreadReadyDel(thread);
      thread->priority = priority;
      ThreadReadyAdd(thread);
      ThreadDispatch(NULL);
    }
  }
}

/*******************************************************************************
 *  function implementations (scope: module-exported)
 ******************************************************************************/

/**
 * @fn          osThreadId_t osThreadNew(osThreadFunc_t func, void *argument, const osThreadAttr_t *attr)
 * @brief       Create a thread and add it to Active Threads.
 * @param[in]   func      thread function.
 * @param[in]   argument  pointer that is passed to the thread function as start argument.
 * @param[in]   attr      thread attributes.
 * @return      thread ID for reference by other functions or NULL in case of error.
 */
osThreadId_t osThreadNew(osThreadFunc_t func, void *argument, const osThreadAttr_t *attr)
{
  osThreadId_t thread_id;

  if (IsIrqMode() || IsIrqMasked()) {
    thread_id = NULL;
  }
  else {
    thread_id = (osThreadId_t)svc_3((uint32_t)func, (uint32_t)argument, (uint32_t)attr, (uint32_t)ThreadNew);
  }

  return (thread_id);
}

/**
 * @fn          const char *osThreadGetName(osThreadId_t thread_id)
 * @brief       Get name of a thread.
 * @param[in]   thread_id   thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
 * @return      name as null-terminated string.
 */
const char *osThreadGetName(osThreadId_t thread_id)
{
  const char *name;

  if (IsIrqMode() || IsIrqMasked()) {
    name = NULL;
  }
  else {
    name = (const char *)svc_1((uint32_t)thread_id, (uint32_t)ThreadGetName);
  }

  return (name);
}

/**
 * @fn          osThreadId_t osThreadGetId(void)
 * @brief       Return the thread ID of the current running thread.
 * @return      thread ID for reference by other functions or NULL in case of error.
 */
osThreadId_t osThreadGetId(void)
{
  osThreadId_t thread_id;

  if (IsIrqMode() || IsIrqMasked()) {
    thread_id = ThreadGetId();
  }
  else {
    thread_id = (osThreadId_t)svc_0((uint32_t)ThreadGetId);
  }

  return (thread_id);
}

/**
 * @fn          osThreadState_t osThreadGetState(osThreadId_t thread_id)
 * @brief       Get current thread state of a thread.
 * @param[in]   thread_id   thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
 * @return      current thread state of the specified thread.
 */
osThreadState_t osThreadGetState(osThreadId_t thread_id)
{
  osThreadState_t state;

  if (IsIrqMode() || IsIrqMasked()) {
    state = osThreadError;
  }
  else {
    state = (osThreadState_t)svc_1((uint32_t)thread_id, (uint32_t)ThreadGetState);
  }

  return (state);
}

/**
 * @fn          uint32_t osThreadGetStackSize(osThreadId_t thread_id)
 * @brief       Get stack size of a thread.
 * @param[in]   thread_id   thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
 * @return      stack size in bytes.
 */
uint32_t osThreadGetStackSize(osThreadId_t thread_id)
{
  uint32_t stack_size;

  if (IsIrqMode() || IsIrqMasked()) {
    stack_size = 0U;
  }
  else {
    stack_size = svc_1((uint32_t)thread_id, (uint32_t)ThreadGetStackSize);
  }

  return (stack_size);
}

/**
 * @fn          uint32_t osThreadGetStackSpace(osThreadId_t thread_id)
 * @brief       Get available stack space of a thread based on stack watermark recording during execution.
 * @param[in]   thread_id   thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
 * @return      remaining stack space in bytes.
 */
uint32_t osThreadGetStackSpace(osThreadId_t thread_id)
{
  uint32_t stack_space;

  if (IsIrqMode() || IsIrqMasked()) {
    stack_space = 0U;
  }
  else {
    stack_space = svc_1((uint32_t)thread_id, (uint32_t)ThreadGetStackSpace);
  }

  return (stack_space);
}

/**
 * @fn          osStatus_t osThreadSetPriority(osThreadId_t thread_id, osPriority_t priority)
 * @brief       Change priority of a thread.
 * @param[in]   thread_id   thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
 * @param[in]   priority    new priority value for the thread function.
 * @return      status code that indicates the execution status of the function.
 */
osStatus_t osThreadSetPriority(osThreadId_t thread_id, osPriority_t priority)
{
  osStatus_t status;

  if (IsIrqMode() || IsIrqMasked()) {
    status = osErrorISR;
  }
  else {
    status = (osStatus_t)svc_2((uint32_t)thread_id, (uint32_t)priority, (uint32_t)ThreadSetPriority);
  }

  return (status);
}

/**
 * @fn          osPriority_t osThreadGetPriority(osThreadId_t thread_id)
 * @brief       Get current priority of a thread.
 * @param[in]   thread_id   thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
 * @return      current priority value of the specified thread.
 */
osPriority_t osThreadGetPriority(osThreadId_t thread_id)
{
  osPriority_t priority;

  if (IsIrqMode() || IsIrqMasked()) {
    priority = osPriorityError;
  }
  else {
    priority = (osPriority_t)svc_1((uint32_t)thread_id, (uint32_t)ThreadGetPriority);
  }

  return (priority);
}

/**
 * @fn          osStatus_t osThreadYield(void)
 * @brief       Pass control to next thread that is in state READY.
 * @return      status code that indicates the execution status of the function.
 */
osStatus_t osThreadYield(void)
{
  osStatus_t status;

  if (IsIrqMode() || IsIrqMasked()) {
    status = osErrorISR;
  }
  else {
    status = (osStatus_t)svc_0((uint32_t)ThreadYield);
  }

  return (status);
}

/**
 * @fn          osStatus_t osThreadSuspend(osThreadId_t thread_id)
 * @brief       Suspend execution of a thread.
 * @param[in]   thread_id   thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
 * @return      status code that indicates the execution status of the function.
 */
osStatus_t osThreadSuspend(osThreadId_t thread_id)
{
  osStatus_t status;

  if (IsIrqMode() || IsIrqMasked()) {
    status = osErrorISR;
  }
  else {
    status = (osStatus_t)svc_1((uint32_t)thread_id, (uint32_t)ThreadSuspend);
  }

  return (status);
}

/**
 * @fn          osStatus_t osThreadResume(osThreadId_t thread_id)
 * @brief       Resume execution of a thread.
 * @param[in]   thread_id   thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
 * @return      status code that indicates the execution status of the function.
 */
osStatus_t osThreadResume(osThreadId_t thread_id)
{
  osStatus_t status;

  if (IsIrqMode() || IsIrqMasked()) {
    status = osErrorISR;
  }
  else {
    status = (osStatus_t)svc_1((uint32_t)thread_id, (uint32_t)ThreadResume);
  }

  return (status);
}

/**
 * @fn          void osThreadExit(void)
 * @brief       Terminate execution of current running thread.
 */
__NO_RETURN
void osThreadExit(void)
{
  svc_0((uint32_t)ThreadExit);
  for (;;);
}

/**
 * @fn          osStatus_t osThreadTerminate(osThreadId_t thread_id)
 * @brief       Terminate execution of a thread.
 * @param[in]   thread_id   thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
 * @return      status code that indicates the execution status of the function.
 */
osStatus_t osThreadTerminate(osThreadId_t thread_id)
{
  osStatus_t status;

  if (IsIrqMode() || IsIrqMasked()) {
    status = osErrorISR;
  }
  else {
    status = (osStatus_t)svc_1((uint32_t)thread_id, (uint32_t)ThreadTerminate);
  }

  return (status);
}

/**
 * @fn          uint32_t osThreadGetCount(void)
 * @brief       Get number of active threads.
 * @return      number of active threads or 0 in case of an error.
 */
uint32_t osThreadGetCount(void)
{
  uint32_t count;

  if (IsIrqMode() || IsIrqMasked()) {
    count = 0U;
  }
  else {
    count = svc_0((uint32_t)ThreadGetCount);
  }

  return (count);
}

/**
 * @fn          uint32_t osThreadEnumerate(osThreadId_t *thread_array, uint32_t array_items)
 * @brief       Enumerate active threads.
 * @param[out]  thread_array  pointer to array for retrieving thread IDs.
 * @param[in]   array_items   maximum number of items in array for retrieving thread IDs.
 * @return      number of enumerated threads or 0 in case of an error.
 */
uint32_t osThreadEnumerate(osThreadId_t *thread_array, uint32_t array_items)
{
  uint32_t count;

  if (IsIrqMode() || IsIrqMasked()) {
    count = 0U;
  }
  else {
    count = svc_2((uint32_t)thread_array, array_items, (uint32_t)ThreadEnumerate);
  }

  return (count);
}

/* ----------------------------- End of file ---------------------------------*/
