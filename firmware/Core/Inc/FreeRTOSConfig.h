/**
 * @file FreeRTOSConfig.h — конфігурація ядра FreeRTOS V11.1.0 для NUCLEO-F411RE (Cortex-M4F, порт GCC/ARM_CM4F).
 * Пояснення кожного параметра, що відрізняється від типового, — у docs/rtos-design.md, розділ «Конфігурація ядра».
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdint.h>

extern uint32_t SystemCoreClock;
void     rtos_assert_failed(const char *file, int line);
void     rtos_trace_switch_in(void);
uint32_t rtos_runtime_cycles(void);

#ifndef RTOS_TICK_HZ
#define RTOS_TICK_HZ                            1000      /* ЛР4, п. 7.2: S3 перевіряється з 1000 і 100 Гц (-DRTOS_TICK_HZ=100) */
#endif
#ifndef RTOS_HEAP_BYTES
#define RTOS_HEAP_BYTES                         (1U * 1024U)       /* ЛР5: AO, черги і задача простою статичні; heap ОС не потрібен (у ЛР4 24 КБ) */
#endif

/* ---- планувальник ---- */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      ((TickType_t)RTOS_TICK_HZ)
#define configMAX_PRIORITIES                    16    /* ЛР5: порт QP вимагає >= QF_MAX_ACTIVE (12); пріоритети задач ті самі, що в ЛР4 (QF_prioSpec) */
#define configMINIMAL_STACK_SIZE                ((uint16_t)128)     /* слів (4 Б) */
#define configMAX_TASK_NAME_LEN                 10
#define configIDLE_SHOULD_YIELD                 1
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

/* ---- пам'ять ---- */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         1     /* ЛР5: QActive_start створює задачу і чергу AO статично */
#define configTOTAL_HEAP_SIZE                   ((size_t)RTOS_HEAP_BYTES)
#define configAPPLICATION_ALLOCATED_HEAP        0
#define configUSE_NEWLIB_REENTRANT              0     /* printf серіалізується м'ютексом (Core/Src/rtos_port.c), окремих reent-структур не потрібно */

/* ---- механізми взаємодії ---- */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_QUEUE_SETS                    0
#define configUSE_STREAM_BUFFERS                1
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_TIMERS                        0     /* програмні таймери не потрібні: усі періоди — vTaskDelayUntil у задачах */
#define configUSE_EVENT_GROUPS                  0
#define configUSE_CO_ROUTINES                   0

/* ---- хуки і діагностика ---- */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     1     /* ЛР5: у хуку тіку QP обслуговує часові події (bsp.c) */
#define configUSE_MALLOC_FAILED_HOOK            1
#ifdef RTOS_NO_SOF
#define configCHECK_FOR_STACK_OVERFLOW          0     /* лише для сценарію ЛР4 п. 7.2: що буде без контролю */
#else
#define configCHECK_FOR_STACK_OVERFLOW          2     /* перевірка шаблоном стека на кожному перемиканні */
#endif
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
#define configGENERATE_RUN_TIME_STATS           1
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() /* DWT вже запущено platform_perf_init() */
#define portGET_RUN_TIME_COUNTER_VALUE()        rtos_runtime_cycles()
#define configASSERT(x)                         do { if ((x) == 0) rtos_assert_failed(__FILE__, __LINE__); } while (0)
#define traceTASK_SWITCHED_IN()                 rtos_trace_switch_in()
void meas_queue_rx(void const *q, uint32_t waiting);
#define traceQUEUE_RECEIVE(pxQueue)             meas_queue_rx((pxQueue), (pxQueue)->uxMessagesWaiting)   /* ЛР5 QSTAT: заповненість черги AO */

/* ---- переривання (Cortex-M4: 4 біти пріоритету, NVIC_PRIORITYGROUP_4) ---- */
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5     /* ISR з FromISR-викликами мають пріоритет 5..15 (EXTI, USART2, TIM2 — 6) */
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* Обробники виключень ядра підключаються до векторів startup-файлу під їхніми CMSIS-іменами */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler

/* ---- API, що потрібне ---- */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTaskAbortDelay                 1     /* PERIOD: нова затримка діє одразу, а не після завершення старої */
#endif
