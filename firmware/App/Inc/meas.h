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

/* ---- сценарії п. 9.2–9.3 (TEST ...): перемикачі вмикає cli, AO лише читають їх. Це тестовий код, як вимірювальні лічильники.
 * Навмисні порушення моделі AO (блокування в обробнику, довгий крок) йдуть через функції Core/Src/qp_tests.c, а не через
 * виклики FreeRTOS у файлах AO: так перевірка п. 4.4 лишається чистою, а порушення видно лише при ввімкненому сценарії. ---- */
typedef struct {
    volatile uint8_t  block_log;       /* 2: log чекає flash циклом з блокуванням в обробнику, а не подією POLL */
    volatile uint8_t  flash_fail;      /* 3.8: кожна операція flash повертає помилку шини */
    volatile uint16_t owner_wait_ms;   /* 2: log ще й чекає N мс у кожному кроці (власник ресурсу тримає процесор) */
    volatile uint16_t ctrl_work_ms;    /* 3: control робить N мс роботи на кожному SAMPLE */
    volatile uint16_t hog_ms;          /* інверсія: imu (середній пріоритет) займає процесор N мс кожні 100 мс */
    volatile uint16_t log_every_ms;    /* control пише запис EV_USER у журнал кожні N мс (багато вимірів flash_wait) */
    volatile uint32_t snap_timeout_ms; /* 6: таймаут операції знімка (0 = 10 с) */
    volatile uint32_t late_events;     /* 6: запізнілі CAPTURE_DONE / SAVE_DONE, що прийшли в idle */
    volatile uint8_t  one_step_save;   /* 3: log пише весь кадр одним кроком */
} meas_test_t;
extern meas_test_t g_test;
void meas_block_ms(uint32_t ms);      /* ЗАБОРОНЕНЕ в AO очікування з блокуванням задачі (лише сценарій 2) */
void meas_spin_ms(uint32_t ms);       /* робота процесора N мс без блокування (сценарії 2, 3, інверсія) */
void meas_flash_lock(void);           /* лише варіант LR5_FLASH_MUTEX (сценарій 3.1): м'ютекс flash, як у ЛР4 */
void meas_flash_unlock(void);

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
