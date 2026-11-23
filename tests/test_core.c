#include <string.h>
#include "test.h"
#include "crc16.h"
#include "perf.h"
#include "sched.h"
#include "ringbuf.h"
#include "rtstats.h"

/* ---- crc16 ---- */
static void crc16_known_vector(void)
{
    CHECK_EQ(crc16((const uint8_t *)"123456789", 9), 0x29B1);    /* CRC-16/CCITT-FALSE check value */
    CHECK_EQ(crc16(NULL, 0), 0xFFFF);
}

/* ---- perf ---- */
static void perf_stats_min_avg_max(void)
{
    perf_stat_t s; perf_stat_init(&s, "x");
    CHECK_EQ(perf_avg(&s), 0);
    perf_add(&s, 10); perf_add(&s, 30); perf_add(&s, 20);
    CHECK_EQ(s.count, 3); CHECK_EQ(s.min, 10); CHECK_EQ(s.max, 30); CHECK_EQ(perf_avg(&s), 20);
}

static void perf_hist_bins_and_percentile(void)
{
    perf_hist_t h; perf_hist_init(&h, 100);
    CHECK_EQ(perf_hist_percentile(&h, 99), 0);                  /* порожньо */
    for (int i = 0; i < 99; i++) perf_hist_add(&h, 50);        /* комірка 0 */
    perf_hist_add(&h, 250);                                     /* комірка 2 */
    perf_hist_add(&h, 100000);                                  /* переповнення -> остання комірка */
    CHECK_EQ(h.bins[0], 99); CHECK_EQ(h.bins[2], 1); CHECK_EQ(h.bins[PERF_HIST_BINS - 1], 1);
    CHECK_EQ(perf_hist_percentile(&h, 50), 100);
    CHECK_EQ(perf_hist_percentile(&h, 100), PERF_HIST_BINS * 100);
    perf_hist_t z; perf_hist_init(&z, 0);                       /* нульова ширина -> 1 */
    CHECK_EQ(z.bin_width, 1);
}

/* ---- sched: детермінований годинник ---- */
static uint32_t fake_us;
static uint32_t now_us(void)  { return fake_us; }
static uint32_t now_cyc(void) { return fake_us * 100U; }       /* 100 МГц */
static int a_runs, b_runs;
static void task_a(void) { a_runs++; fake_us += 50; }          /* виконується 50 мкс */
static void task_b(void) { b_runs++; fake_us += 3000; }        /* довга: 3 мс */

static void sched_runs_on_time_and_measures(void)
{
    task_t t[2] = { TASK_DEF("a", task_a, 1000), TASK_DEF("b", task_b, 10000) };
    sched_t s; fake_us = 0; a_runs = b_runs = 0;
    sched_init(&s, t, 2, now_us, now_cyc);
    CHECK_EQ(sched_run_once(&s), 0);                            /* рано */
    fake_us = 1000;
    CHECK_EQ(sched_run_once(&s), 1); CHECK_EQ(a_runs, 1);
    CHECK_EQ(t[0].late_us.max, 0);
    CHECK_EQ(t[0].exec_cyc.max, 5000);                          /* 50 мкс = 5000 тактів */
    fake_us = 2000;
    sched_run_once(&s);
    CHECK_EQ(t[0].runs, 2);
}

static void sched_counts_missed_periods_after_long_task(void)
{
    task_t t[2] = { TASK_DEF("a", task_a, 1000), TASK_DEF("b", task_b, 5000) };
    sched_t s; fake_us = 0; a_runs = b_runs = 0;
    sched_init(&s, t, 2, now_us, now_cyc);
    fake_us = 5000;
    sched_run_once(&s);                     /* a: 4 періоди пропущено? ні — перший запуск у момент 5000: запізнення 4000 */
    CHECK_EQ(t[0].missed, 4);
    CHECK_EQ(t[0].late_us.max, 0);          /* після зсуву плану запізнення 0 */
    CHECK_EQ(t[1].runs, 1);
    CHECK(s.busy_cyc >= (uint64_t)3000 * 100);
    sched_reset_stats(&s);
    CHECK_EQ(t[0].runs, 0); CHECK_EQ(s.busy_cyc, 0);
}

static void sched_late_start_is_jitter(void)
{
    task_t t[1] = { TASK_DEF("a", task_a, 1000) };
    sched_t s; fake_us = 0;
    sched_init(&s, t, 1, now_us, now_cyc);
    fake_us = 1300;                          /* запізнились на 300 мкс, але не на весь період */
    sched_run_once(&s);
    CHECK_EQ(t[0].missed, 0); CHECK_EQ(t[0].late_us.max, 300);
    CHECK_EQ(t[0].late_hist.bins[3], 1);
}

/* ---- ringbuf ---- */
static void ring_fifo_and_overflow(void)
{
    uint8_t mem[4]; ring_t r; ring_init(&r, mem, sizeof mem);        /* ємність 3 */
    CHECK_EQ(ring_get(&r), -1);
    CHECK_EQ(ring_put(&r, 1), 1); CHECK_EQ(ring_put(&r, 2), 1); CHECK_EQ(ring_put(&r, 3), 1);
    CHECK_EQ(ring_level(&r), 3);
    CHECK_EQ(ring_put(&r, 4), 0); CHECK_EQ(r.overflow, 1);           /* повний: байт втрачено */
    CHECK_EQ(ring_get(&r), 1); CHECK_EQ(ring_put(&r, 5), 1);
    CHECK_EQ(ring_get(&r), 2); CHECK_EQ(ring_get(&r), 3); CHECK_EQ(ring_get(&r), 5);
    CHECK_EQ(ring_get(&r), -1);
}


/* ---- rtstats: облік часу за задачами і журнал перемикань ---- */
static void rts_accounts_time_to_previous_task(void)
{
    rtstats_t s; rts_init(&s, 1000);
    rts_switch(&s, 10, 1000);                 /* задача 10 отримала процесор у 1000 */
    rts_switch(&s, 20, 1300);                 /* 10 працювала 300 тактів */
    rts_switch(&s, 10, 1350);                 /* 20 працювала 50 */
    rts_flush(&s, 1500);                      /* 10 працює ще 150 */
    CHECK_EQ(rts_run_cyc(&s, 10), 450); CHECK_EQ(rts_run_cyc(&s, 20), 50);
    CHECK_EQ(rts_total_cyc(&s), 500); CHECK_EQ(s.total_switches, 3); CHECK_EQ(s.t[0].switches, 2);
    CHECK_EQ(rts_run_cyc(&s, 99), 0);
}

static void rts_survives_dwt_wraparound(void)
{
    rtstats_t s; rts_init(&s, 0xFFFFFF00u);
    rts_switch(&s, 1, 0xFFFFFF00u);
    rts_switch(&s, 2, 0x00000100u);           /* лічильник переповнився: пройшло 0x200 тактів */
    CHECK_EQ(rts_run_cyc(&s, 1), 0x200);
}

static void rts_reset_keeps_task_table(void)
{
    rtstats_t s; rts_init(&s, 0);
    rts_switch(&s, 1, 0); rts_switch(&s, 2, 100);
    rts_reset(&s, 500);
    CHECK_EQ(s.n, 2); CHECK_EQ(rts_total_cyc(&s), 0); CHECK_EQ(s.total_switches, 0);
    rts_flush(&s, 700);
    CHECK_EQ(rts_run_cyc(&s, 2), 200);        /* поточна задача лишилась тією самою */
}

static void rts_table_full_counts_unknown(void)
{
    rtstats_t s; rts_init(&s, 0);
    for (uint32_t i = 1; i <= RTS_MAX_TASKS + 2; i++) rts_switch(&s, i, i * 10);
    CHECK_EQ(s.n, RTS_MAX_TASKS); CHECK_EQ(s.unknown, 2);
}

static void rts_trace_records_and_stops_when_full(void)
{
    rtstats_t s; rts_init(&s, 0);
    rts_switch(&s, 1, 10);                    /* ще не озброєно: не записується */
    CHECK_EQ(s.ev_count, 0);
    rts_trace_arm(&s);
    rts_switch(&s, 2, 20); rts_switch(&s, 3, 30);
    CHECK_EQ(s.ev_count, 2); CHECK_EQ(s.ev[0].id, 2); CHECK_EQ(s.ev[1].cyc, 30);
    for (int i = 0; i < RTS_TRACE_N + 5; i++) rts_switch(&s, 4, (uint32_t)(100 + i));
    CHECK_EQ(s.ev_count, RTS_TRACE_N); CHECK_EQ(s.trace_on, 0);
}

void test_core(void)
{
    RUN(crc16_known_vector); RUN(perf_stats_min_avg_max); RUN(perf_hist_bins_and_percentile);
    RUN(sched_runs_on_time_and_measures); RUN(sched_counts_missed_periods_after_long_task); RUN(sched_late_start_is_jitter);
    RUN(ring_fifo_and_overflow);
    RUN(rts_accounts_time_to_previous_task); RUN(rts_survives_dwt_wraparound); RUN(rts_reset_keeps_task_table);
    RUN(rts_table_full_counts_unknown); RUN(rts_trace_records_and_stops_when_full);
}
