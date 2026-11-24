/**
 * @file rtstats.h — облік процесорного часу за задачами і журнал перемикань контексту (ЛР4, п. 6.2 і 6.5).
 * Логіка без заліза: ядро викликає rts_switch() з traceTASK_SWITCHED_IN, час приходить аргументом (такти DWT).
 * Ідентифікатор задачі — довільне число (у прошивці це адреса TCB).
 */
#ifndef RTSTATS_H
#define RTSTATS_H
#include <stdint.h>

#define RTS_MAX_TASKS 16
#define RTS_TRACE_N   1024

typedef struct { uint32_t id; uint64_t run_cyc; uint32_t switches; } rts_task_t;
typedef struct { uint32_t cyc; uint32_t id; } rts_ev_t;

typedef struct {
    rts_task_t t[RTS_MAX_TASKS];
    int        n;
    uint32_t   cur_id, last_cyc;
    uint32_t   total_switches;
    uint32_t   unknown;                      /* перемикань на задачу, для якої не лишилось місця в таблиці */
    rts_ev_t   ev[RTS_TRACE_N];
    uint32_t   ev_count;
    int        trace_on;
} rtstats_t;

void     rts_init(rtstats_t *s, uint32_t cyc);
/** Задача id отримала процесор у момент cyc: час від попереднього перемикання зараховується попередній задачі. */
void     rts_switch(rtstats_t *s, uint32_t id, uint32_t cyc);
/** Скинути лічильники (таблицю задач лишає), початок нового вікна — cyc. */
void     rts_reset(rtstats_t *s, uint32_t cyc);
/** Додати до поточної задачі час до моменту cyc (для читання статистики посеред ходу). */
void     rts_flush(rtstats_t *s, uint32_t cyc);
uint64_t rts_run_cyc(const rtstats_t *s, uint32_t id);
uint64_t rts_total_cyc(const rtstats_t *s);
/** Журнал перемикань: armed = записати перші RTS_TRACE_N перемикань і зупинитись. */
void     rts_trace_arm(rtstats_t *s);
#endif
