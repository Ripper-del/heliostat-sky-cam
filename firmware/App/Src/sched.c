#include "sched.h"

static void task_reset(task_t *t)
{
    t->runs = t->missed = 0;
    perf_stat_init(&t->exec_cyc, t->name);
    perf_stat_init(&t->late_us, t->name);
    perf_hist_init(&t->late_hist, 100);
}

void sched_init(sched_t *s, task_t *tasks, size_t n, uint32_t (*now_us)(void), uint32_t (*now_cyc)(void))
{
    s->tasks = tasks; s->n = n; s->now_us = now_us; s->now_cyc = now_cyc; s->busy_cyc = 0;
    uint32_t now = now_us();
    for (size_t i = 0; i < n; i++) { task_reset(&tasks[i]); tasks[i].due_us = now + tasks[i].period_us; }
}

void sched_reset_stats(sched_t *s)
{
    s->busy_cyc = 0;
    for (size_t i = 0; i < s->n; i++) task_reset(&s->tasks[i]);
}

int sched_run_once(sched_t *s)
{
    int ran = 0;
    for (size_t i = 0; i < s->n; i++) {
        task_t *t = &s->tasks[i];
        uint32_t now = s->now_us();
        int32_t late = (int32_t)(now - t->due_us);
        if (late < 0) continue;                                   /* ще не час */
        uint32_t skipped = (uint32_t)late / t->period_us;         /* повністю пропущені періоди */
        if (skipped) { t->missed += skipped; t->due_us += skipped * t->period_us; late -= (int32_t)(skipped * t->period_us); }
        perf_add(&t->late_us, (uint32_t)late);
        perf_hist_add(&t->late_hist, (uint32_t)late);
        uint32_t c0 = s->now_cyc();
        t->fn();
        uint32_t used = s->now_cyc() - c0;
        perf_add(&t->exec_cyc, used);
        s->busy_cyc += used;
        t->runs++; t->due_us += t->period_us; ran++;
    }
    return ran;
}
