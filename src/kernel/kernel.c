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

#include "string.h"
#include "os_lib.h"

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
 *  function prototypes (scope: module-local)
 ******************************************************************************/

/*******************************************************************************
 *  function implementations (scope: module-local)
 ******************************************************************************/

void osTick_Handler(void)
{
  osTimer_t *timer;
  queue_t   *timer_queue;

  ++osInfo.kernel.tick;

  /* Process Timers */
  if (osInfo.timer_semaphore != NULL) {
    timer_queue = &osInfo.timer_queue;
    if (!isQueueEmpty(timer_queue)) {
      timer = GetTimerByQueue(timer_queue->next);
      if (time_before_eq(timer->time, osInfo.kernel.tick)) {
        osSemaphoreRelease(osInfo.timer_semaphore);
      }
    }
  }

  BEGIN_CRITICAL_SECTION

  /* Process Thread Delays */
  if (libThreadDelayTick() == true) {
    libThreadDispatch(NULL);
  }

  END_CRITICAL_SECTION
}

static osStatus_t KernelInitialize(void)
{
  if (osInfo.kernel.state == osKernelReady) {
    return osOK;
  }

  if (osInfo.kernel.state != osKernelInactive) {
    return osError;
  }

  /* Initialize osInfo */
  memset(&osInfo, 0, sizeof(osInfo));

  for (uint32_t i = 0U; i < NUM_PRIORITY; i++) {
    QueueReset(&osInfo.ready_list[i]);
  }

  QueueReset(&osInfo.timer_queue);
  QueueReset(&osInfo.delay_queue);

  osInfo.kernel.state = osKernelReady;

  return (osOK);
}

static osStatus_t KernelGetInfo(osVersion_t *version, char *id_buf, uint32_t id_size)
{
  if (version != NULL) {
    version->api    = osVersionAPI;
    version->kernel = osVersionKernel;
  }

  if ((id_buf != NULL) && (id_size != 0U)) {
    if (id_size > sizeof(osKernelId)) {
      id_size = sizeof(osKernelId);
    }
    memcpy(id_buf, osKernelId, id_size);
  }

  return (osOK);
}

static osKernelState_t KernelGetState(void)
{
  return (osInfo.kernel.state);
}

static osStatus_t KernelStart(void)
{
  osThread_t *thread;
  uint32_t sh;

  if (osInfo.kernel.state != osKernelReady) {
    return osError;
  }

  /* Thread startup (Idle and Timer Thread) */
  if (!libThreadStartup()) {
    return osError;
  }

  /* Setup SVC and PendSV System Service Calls */
  sh = SystemIsrInit();
  osInfo.base_priority = (osConfig.max_api_interrupt_priority << sh) & 0x000000FFU;

  /* Setup RTOS Tick */
  osSysTickInit(osConfig.tick_freq);

  /* Switch to Ready Thread with highest Priority */
  thread = libThreadHighestPrioGet();
  if (thread == NULL) {
    return osError;
  }
  libThreadSwitch(thread);

  if ((osConfig.flags & osConfigPrivilegedMode) != 0U) {
    // Privileged Thread mode & PSP
    __set_CONTROL(0x02U);
  } else {
    // Unprivileged Thread mode & PSP
    __set_CONTROL(0x03U);
  }

  osInfo.kernel.state = osKernelRunning;

  return (osOK);
}

static int32_t KernelLock(void)
{
  int32_t lock;

  switch (osInfo.kernel.state) {
    case osKernelRunning:
      osInfo.kernel.state = osKernelLocked;
      lock = 0;
      break;

    case osKernelLocked:
      lock = 1;
      break;

    default:
      lock = (int32_t)osError;
      break;
  }

  return (lock);
}

static int32_t KernelUnlock(void)
{
  int32_t lock;

  switch (osInfo.kernel.state) {
    case osKernelRunning:
      lock = 0;
      break;

    case osKernelLocked:
      osInfo.kernel.state = osKernelRunning;
      lock = 1;
      break;

    default:
      lock = (int32_t)osError;
      break;
  }

  return (lock);
}

static int32_t KernelRestoreLock(int32_t lock)
{
  int32_t lock_new;

  switch (osInfo.kernel.state) {
    case osKernelRunning:
    case osKernelLocked:
      switch (lock) {
        case 0:
          osInfo.kernel.state = osKernelRunning;
          lock_new = 0;
          break;

        case 1:
          osInfo.kernel.state = osKernelLocked;
          lock_new = 1;
          break;

        default:
          lock_new = (int32_t)osError;
          break;
      }
      break;

    default:
      lock_new = (int32_t)osError;
      break;
  }

  return (lock_new);
}

static uint32_t KernelGetTickCount(void)
{
  return (osInfo.kernel.tick);
}

static uint32_t KernelGetTickFreq(void)
{
  return (osConfig.tick_freq);
}

/*******************************************************************************
 *  function implementations (scope: module-exported)
 ******************************************************************************/

/**
 * @fn          osStatus_t osKernelInitialize(void)
 * @brief       Initialize the RTOS Kernel.
 * @return      status code that indicates the execution status of the function.
 */
osStatus_t osKernelInitialize(void)
{
  osStatus_t status;

  if (IsIrqMode() || IsIrqMasked()) {
    status = osErrorISR;
  }
  else {
    status = (osStatus_t)svc_0((uint32_t)KernelInitialize);
  }

  return (status);
}

/**
 * @fn          osStatus_t osKernelGetInfo(osVersion_t *version, char *id_buf, uint32_t id_size)
 * @brief       Get RTOS Kernel Information.
 * @param[out]  version   pointer to buffer for retrieving version information.
 * @param[out]  id_buf    pointer to buffer for retrieving kernel identification string.
 * @param[in]   id_size   size of buffer for kernel identification string.
 * @return      status code that indicates the execution status of the function.
 */
osStatus_t osKernelGetInfo(osVersion_t *version, char *id_buf, uint32_t id_size)
{
  osStatus_t status;

  if (IsIrqMode() || IsIrqMasked()) {
    status = KernelGetInfo(version, id_buf, id_size);
  }
  else {
    status = (osStatus_t)svc_3((uint32_t)version, (uint32_t)id_buf, id_size, (uint32_t)KernelGetInfo);
  }

  return (status);
}

/**
 * @fn          osKernelState_t osKernelGetState(void)
 * @brief       Get the current RTOS Kernel state.
 * @return      current RTOS Kernel state.
 */
osKernelState_t osKernelGetState(void)
{
  osKernelState_t state;

  if (IsIrqMode() || IsIrqMasked()) {
    state = KernelGetState();
  }
  else {
    state = (osKernelState_t)svc_0((uint32_t)KernelGetState);
  }

  return (state);
}

/**
 * @fn          osStatus_t osKernelStart(void)
 * @brief       Start the RTOS Kernel scheduler.
 * @return      status code that indicates the execution status of the function.
 */
osStatus_t osKernelStart(void)
{
  osStatus_t status;

  if (IsIrqMode() || IsIrqMasked()) {
    status = osErrorISR;
  }
  else {
    status = (osStatus_t)svc_0((uint32_t)KernelStart);
  }

  return (status);
}

/**
 * @fn          int32_t osKernelLock(void)
 * @brief       Lock the RTOS Kernel scheduler.
 * @return      previous lock state (1 - locked, 0 - not locked, error code if negative).
 */
int32_t osKernelLock(void)
{
  int32_t lock;

  if (IsIrqMode() || IsIrqMasked()) {
    lock = (int32_t)osErrorISR;
  }
  else {
    lock = svc_0((uint32_t)KernelLock);
  }

  return (lock);
}

/**
 * @fn          int32_t osKernelUnlock(void)
 * @brief       Unlock the RTOS Kernel scheduler.
 * @return      previous lock state (1 - locked, 0 - not locked, error code if negative).
 */
int32_t osKernelUnlock(void)
{
  int32_t lock;

  if (IsIrqMode() || IsIrqMasked()) {
    lock = (int32_t)osErrorISR;
  }
  else {
    lock = svc_0((uint32_t)KernelUnlock);
  }

  return (lock);
}

/**
 * @fn          int32_t osKernelRestoreLock(int32_t lock)
 * @brief       Restore the RTOS Kernel scheduler lock state.
 * @param[in]   lock  lock state obtained by \ref osKernelLock or \ref osKernelUnlock.
 * @return      new lock state (1 - locked, 0 - not locked, error code if negative).
 */
int32_t osKernelRestoreLock(int32_t lock)
{
  int32_t lock_new;

  if (IsIrqMode() || IsIrqMasked()) {
    lock_new = (int32_t)osErrorISR;
  }
  else {
    lock_new = svc_1((uint32_t)lock, (uint32_t)KernelRestoreLock);
  }

  return (lock_new);
}

/**
 * @fn          uint32_t osKernelGetTickCount(void)
 * @brief       Get the RTOS kernel tick count.
 * @return      RTOS kernel current tick count.
 */
uint32_t osKernelGetTickCount(void)
{
  uint32_t count;

  if (IsIrqMode() || IsIrqMasked()) {
    count = KernelGetTickCount();
  }
  else {
    count =  svc_0((uint32_t)KernelGetTickCount);
  }

  return (count);
}

/**
 * @fn          uint32_t osKernelGetTickFreq(void)
 * @brief       Get the RTOS kernel tick frequency.
 * @return      frequency of the kernel tick in hertz, i.e. kernel ticks per second.
 */
uint32_t osKernelGetTickFreq(void)
{
  uint32_t freq;

  if (IsIrqMode() || IsIrqMasked()) {
    freq = KernelGetTickFreq();
  }
  else {
    freq = svc_0((uint32_t)KernelGetTickFreq);
  }

  return (freq);
}

/*------------------------------ End of file ---------------------------------*/
