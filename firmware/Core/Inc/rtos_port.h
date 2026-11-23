#ifndef RTOS_PORT_H
#define RTOS_PORT_H
#include <stdint.h>
#include "rtstats.h"
extern rtstats_t g_rts;                              /* облік часу за задачами і журнал перемикань */
void     rtos_print_init(void);                      /* створити м'ютекс printf (до старту планувальника) */
void     rtos_tick_cost(uint32_t cyc);               /* з SysTick_Handler */
void     rtos_tick_stats(uint32_t *n, uint32_t *avg_cyc, uint32_t *max_cyc);
void     rtos_tick_reset(void);
void     rtos_tick_gap(uint32_t *gap_max_us, uint32_t *late_gaps);   /* найбільший інтервал між двома входами в SysTick; кількість інтервалів > 1,5 мс (втрачені тіки) */
void     rtos_tick_gap_reset(void);
void     rtos_switch_gap(uint32_t *gap_us, uint32_t *task);   /* найдовший інтервал без перемикань, коли працювала не-idle задача (діагностика «хто тримає процесор») */
void     rtos_switch_gap_reset(void);
uint32_t rtos_tick_time(uint32_t tick);       /* ідеальний момент тіка (DWT): SysTick->VAL при вході в ISR віднімається */
uint32_t rtos_fault_count(void);
uint32_t rtos_malloc_failed_count(void);
#endif
