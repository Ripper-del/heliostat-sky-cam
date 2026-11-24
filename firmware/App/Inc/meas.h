/**
 * @file meas.h — ЛР5: вимірювальний код (як у ЛР3–ЛР4: DWT, STAT, TASKS, STACK, ...). Лічильники тут — єдиний дозволений
 * виняток зі «спільних змінних» (п. 4.1 методички): AO лише додають у них значення, читає їх cli для друку.
 * Формат рядків той самий, що в ЛР4, тому tools/ працюють без змін.
 */
#ifndef MEAS_H
#define MEAS_H
#include <stdint.h>
#include "perf.h"
#include "ao.h"

typedef struct {
    const char *name;
    uint16_t    stack_words, queue_len;
    uint8_t     qp_prio, os_prio;
    uint32_t    period_ms;            /* 0 — AO, що працює лише від подій */
    QActive    *ao;
    uint32_t    runs, missed, stale;  /* stale — застаріла періодична подія з черги (період уже врахований як пропущений) */
    perf_stat_t exec_cyc;             /* тривалість кроку обробки (RTC-крок) або операції; ВКЛЮЧАЄ час витіснення */
    perf_stat_t late_us;              /* запізнення початку обробки PERIOD відносно ідеального тіка (джитер) */
    perf_hist_t late_hist;
    uint32_t    base_tick, last_k;
    uint8_t     q_max;                /* найбільша кількість подій у черзі AO в момент забирання (trace-макрос FreeRTOS) */
} meas_rt_t;
extern meas_rt_t g_rt[AO_COUNT];

/* окремі метрики (як у ЛР4) + ЛР5: очікування flash від LOG_REQ до кінця запису */
enum { M_BTN, M_EVT, M_CAM_FRAME, M_CMD, M_FLASH_WAIT, M_COUNT };
void meas_add(int which, uint32_t cyc);

/* лічильники подій і помилок (рядок LOAD) */
extern volatile uint32_t g_btn_presses, g_btn_lost, g_rx_lost, g_pend_lost, g_flash_err, g_imu_err, g_cam_err, g_ev_lost, g_text_lost;
extern volatile int32_t  g_flash_last_rc;

void meas_init(void);
void meas_period_arm(int id);                     /* викликати, коли AO запускає (або перезапускає) свою періодичну часову подію */
int  meas_period_begin(int id, uint32_t t0, uint32_t *next_ticks);   /* на початку PERIOD: джитер і пропуски; *next_ticks — до наступного ідеального тіка */
void meas_step(int id, uint32_t t0);              /* кінець кроку: тривалість від t0 */
void meas_defer_min(uint16_t used, uint16_t len); /* заповненість черги відкладених подій log */
uint32_t meas_load_update(void);                /* tick раз на секунду: навантаження CPU за останнє вікно, % */

/* друк — лише з cli (власник UART) */
void meas_print_stats(void);
void meas_reset_stats(void);
void meas_print_tasks(void);
void meas_print_stack(void);
void meas_print_heap(void);
void meas_print_oscost(void);
void meas_print_qstat(void);
void meas_print_mem(void);
void meas_print_version(void);
void meas_trace_cmd(int argc, char **argv);
void meas_cmd_started(void);
void meas_probe_test(void);
#endif
