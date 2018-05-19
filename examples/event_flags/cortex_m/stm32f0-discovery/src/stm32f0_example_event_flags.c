/*
 * Copyright (C) 2015 Sergey Koshkin <koshkin.sergey@gmail.com>
 * All rights reserved
 *
 * File Name  :	tn_stm32f0_example_basic.c
 * Description:	tn_stm32f0_example_basic
 */

/*******************************************************************************
 *  includes
 ******************************************************************************/

#include "ukernel.h"
#include "stm32f0xx.h"
#include "system_stm32f0xx.h"

/*******************************************************************************
 *  external declarations
 ******************************************************************************/

/*******************************************************************************
 *  defines and macros (scope: module-local)
 ******************************************************************************/

#define HZ    1000

#define TASK_A_STK_SIZE   osStackSizeMin
#define TASK_B_STK_SIZE   osStackSizeMin

#define TASK_A_PRIORITY   1
#define TASK_B_PRIORITY   2

/*******************************************************************************
 *  typedefs and structures (scope: module-local)
 ******************************************************************************/

/*******************************************************************************
 *  global variable definitions  (scope: module-exported)
 ******************************************************************************/

/*******************************************************************************
 *  global variable definitions (scope: module-local)
 ******************************************************************************/

static osTask_t task_A;
static osTask_t task_B;
static osEventFlags_t event;

osStack_t task_A_stack[TASK_A_STK_SIZE];
osStack_t task_B_stack[TASK_B_STK_SIZE];

/*******************************************************************************
 *  function prototypes (scope: module-local)
 ******************************************************************************/

/*******************************************************************************
 *  function implementations (scope: module-local)
 ******************************************************************************/

__NO_RETURN
static void task_A_func(void *param)
{
  uint32_t flags;

  for (;;) {
    flags = osEventFlagsWait(&event, 1UL, osFlagsWaitAny, TIME_WAIT_INFINITE);

    if (flags == 1UL) {
      GPIOC->ODR ^= (1UL << 8);
    }
  }
}

__NO_RETURN
static void task_B_func(void *param)
{
  for (;;) {
    osEventFlagsSet(&event, 1UL);

    GPIOC->ODR |= (1UL << 9);
    osTaskSleep(50);
    GPIOC->ODR &= ~(1UL << 9);

    osTaskSleep(950);
  }
}

static
void app_init(void)
{
  /* Инициализируем железо */
  RCC->AHBENR |= RCC_AHBENR_GPIOCEN;

  GPIOC->MODER |= (GPIO_MODER_MODER8_0 | GPIO_MODER_MODER9_0);

  osEventFlagsNew(&event);

  osTaskCreate(
    &task_A,                   // TCB задачи
    task_A_func,               // Функция задачи
    TASK_A_PRIORITY,           // Приоритет задачи
    &(task_A_stack[TASK_A_STK_SIZE-1]),
    TASK_A_STK_SIZE,           // Размер стека (в int, не в байтах)
    NULL,                      // Параметры функции задачи
    osTaskStartOnCreating  // Параметр создания задачи
  );

  osTaskCreate(
    &task_B,                   // TCB задачи
    task_B_func,               // Функция задачи
    TASK_B_PRIORITY,           // Приоритет задачи
    &(task_B_stack[TASK_B_STK_SIZE-1]),
    TASK_B_STK_SIZE,           // Размер стека (в int, не в байтах)
    NULL,                      // Параметры функции задачи
    osTaskStartOnCreating  // Параметр создания задачи
  );
}

/*******************************************************************************
 *  function implementations (scope: module-exported)
 ******************************************************************************/

int main(void)
{
  TN_OPTIONS  tn_opt;

  /* Старт операционной системы */
  tn_opt.app_init       = app_init;
  tn_opt.freq_timer     = HZ;
  osKernelStart(&tn_opt);

  return (-1);
}

void osSysTickInit(unsigned int hz)
{
  /* Включаем прерывание системного таймера именно здесь, после инициализации
     всех сервисов RTOS */
  SystemCoreClockUpdate();
  SysTick_Config(SystemCoreClock/hz);
}

void SysTick_Handler(void)
{
  osTimerHandle();
}

/* ----------------------------- End of file ---------------------------------*/

