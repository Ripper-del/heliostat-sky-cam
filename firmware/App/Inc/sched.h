/**
 * @file sched.h — часовий планувальник super loop (time-triggered): таблиця задач з періодами.
 * Не витісняє: задача, що вже почалась, виконується до кінця. Для кожної задачі міряє час виконання
 * (у тактах), запізнення старту відносно плану (джитер), пропущені періоди. Час приходить через вказівники на функції,
 * тому на хості це детермінований фейковий годинник.
 */
#ifndef SCHED_H
#define SCHED_H
#include <stdint.h>
#include <stddef.h>
#include "perf.h"

typedef void (*sched_fn)(void);

typedef struct {
    const char *name;
    sched_fn    fn;
    uint32_t    period_us;
    uint32_t    due_us;          /* момент наступного запуску за планом */
    uint32_t    runs, missed;    /* запуски; повністю пропущені періоди (запізнення >= періоду) */
    perf_stat_t exec_cyc;        /* час виконання, такти */
    perf_stat_t late_us;         /* запізнення старту відносно плану, мкс */
    perf_hist_t late_hist;       /* розподіл запізнення, комірка 100 мкс */
} task_t;

/** Рядок таблиці задач: ім'я, функція, період у мкс (решта полів заповнює sched_init). */
#define TASK_DEF(nm, f, per_us) { .name = (nm), .fn = (f), .period_us = (per_us) }

typedef struct {
    task_t *tasks; size_t n;
    uint32_t (*now_us)(void);
    uint32_t (*now_cyc)(void);
    uint64_t busy_cyc;           /* сумарний час виконання задач (для завантаження CPU) */
} sched_t;

void sched_init(sched_t *s, task_t *tasks, size_t n, uint32_t (*now_us)(void), uint32_t (*now_cyc)(void));
/** Один прохід: кожна задача, чий час настав, запускається (за порядком у таблиці = за пріоритетом). Повертає кількість запусків. */
int  sched_run_once(sched_t *s);
void sched_reset_stats(sched_t *s);
#endif
