/**
 * @file meas.c — ЛР5: вимірювальний код. Те саме, що діагностика ЛР4 (product_rtos.c): STAT, LOAD, TASKS, STACK, HEAP,
 * OSCOST, TRACE — у тому самому форматі. Нове: QSTAT (заповненість черг AO і пулів подій) і flash_wait.
 * Тут можна викликати FreeRTOS (п. 4.1, правило 9: вимірювальний код).
 */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "meas.h"
#include "platform.h"
#include "rtstats.h"
#include "rtos_port.h"

meas_rt_t g_rt[AO_COUNT];
static perf_stat_t st[M_COUNT];
static const char * const st_name[M_COUNT] = { "btn_latency", "evt_latency", "cam_frame", "cmd_latency", "flash_wait" };
volatile uint32_t g_btn_presses, g_btn_lost, g_rx_lost, g_pend_lost, g_flash_err, g_imu_err, g_cam_err, g_ev_lost, g_text_lost;
volatile int32_t  g_flash_last_rc;
static uint32_t last_load_pct; static uint64_t win_busy_base; static uint32_t win_cyc0;
static uint16_t defer_max_used, defer_len;
extern rtstats_t g_rts;

static uint32_t cyc2us(uint32_t c) { return c / (platform_cpu_hz() / 1000000U); }

static void rt_reset_one(meas_rt_t *r)
{
    r->runs = 0; r->missed = 0; r->stale = 0; r->q_max = 0;
    perf_stat_init(&r->exec_cyc, r->name); perf_stat_init(&r->late_us, r->name); perf_hist_init(&r->late_hist, 100);
}

void meas_init(void)
{
    for (int i = 0; i < AO_COUNT; i++) rt_reset_one(&g_rt[i]);
    for (int i = 0; i < M_COUNT; i++) perf_stat_init(&st[i], st_name[i]);
    rts_init(&g_rts, platform_cycles());
    win_cyc0 = platform_cycles();
}

void meas_add(int which, uint32_t cyc)
{
    taskENTER_CRITICAL(); perf_add(&st[which], cyc); taskEXIT_CRITICAL();
}

/* ---- періодичні AO: той самий облік, що в ЛР4 (periodic() у product_rtos.c) ----
 * У ЛР4 задача прокидалась у vTaskDelayUntil; тут AO отримує PERIOD від одноразової часової події QP, яку сам перезапускає
 * на наступний ІДЕАЛЬНИЙ тік base + (k+1)·period (без накопичення похибки, як vTaskDelayUntil). У черзі AO ніколи не лежить
 * більше однієї події PERIOD, тож довге витіснення не переповнить чергу; повністю пропущені періоди йдуть у missed. */
void meas_period_arm(int id)
{
    meas_rt_t *r = &g_rt[id];
    r->base_tick = xTaskGetTickCount(); r->last_k = 0;
}

int meas_period_begin(int id, uint32_t t0, uint32_t *next_ticks)
{
    meas_rt_t *r = &g_rt[id];
    uint32_t per = MS(r->period_ms); if (!per) per = 1;
    uint32_t now = xTaskGetTickCount();
    uint32_t k = (now - r->base_tick) / per;
    *next_ticks = r->base_tick + (k + 1U) * per - now;          /* до наступного ідеального тіка, >= 1 */
    if (k <= r->last_k) { taskENTER_CRITICAL(); r->stale++; taskEXIT_CRITICAL(); return 0; }
    uint32_t ideal = r->base_tick + k * per;
    int32_t late = (int32_t)(t0 - rtos_tick_time(ideal));
    uint32_t late_us = late > 0 ? (uint32_t)late / (platform_cpu_hz() / 1000000U) : 0U;
    uint32_t skipped = k - r->last_k - 1U;
    r->last_k = k;
    taskENTER_CRITICAL();
    perf_add(&r->late_us, late_us); perf_hist_add(&r->late_hist, late_us); r->missed += skipped;
    taskEXIT_CRITICAL();
    return 1;
}

void meas_step(int id, uint32_t t0)
{
    meas_rt_t *r = &g_rt[id];
    taskENTER_CRITICAL(); perf_add(&r->exec_cyc, platform_cycles() - t0); r->runs++; taskEXIT_CRITICAL();
}

void meas_defer_min(uint16_t used, uint16_t len) { if (used > defer_max_used) defer_max_used = used; defer_len = len; }

/* traceQUEUE_RECEIVE (FreeRTOSConfig.h): скільки подій лежало в черзі AO, коли задача забирала наступну */
void meas_queue_rx(void const *q, uint32_t waiting)
{
    for (int i = 0; i < AO_COUNT; i++) {
        if (g_rt[i].ao && (void const *)g_rt[i].ao->eQueue == q) { if (waiting > g_rt[i].q_max) g_rt[i].q_max = (uint8_t)waiting; return; }
    }
}

/* навантаження: оновлює tick раз на секунду (як tick_work у ЛР4) */
uint32_t meas_load_update(void)
{
    uint32_t now_cyc = platform_cycles();
    taskENTER_CRITICAL(); rts_flush(&g_rts, now_cyc); uint64_t total = rts_total_cyc(&g_rts); uint64_t idl = rts_run_cyc(&g_rts, (uint32_t)xTaskGetIdleTaskHandle()); taskEXIT_CRITICAL();
    uint64_t busy = (total - idl) - win_busy_base; uint32_t window = now_cyc - win_cyc0;
    last_load_pct = window ? (uint32_t)(busy * 100U / window) : 0;
    win_busy_base = total - idl; win_cyc0 = now_cyc;
    return last_load_pct;
}

void meas_print_stats(void)
{
    static meas_rt_t c[AO_COUNT];
    taskENTER_CRITICAL(); memcpy(c, g_rt, sizeof c); taskEXIT_CRITICAL();
    for (int i = 0; i < AO_COUNT; i++) {
        meas_rt_t *t = &c[i];
        printf("STAT name=%s n=%lu min_us=%lu avg_us=%lu max_us=%lu late_max_us=%lu late_p99_us=%lu missed=%lu max_cyc=%lu\r\n",
               t->name, (unsigned long)t->runs, (unsigned long)cyc2us(t->exec_cyc.min), (unsigned long)cyc2us(perf_avg(&t->exec_cyc)),
               (unsigned long)cyc2us(t->exec_cyc.max), (unsigned long)t->late_us.max,
               (unsigned long)perf_hist_percentile(&t->late_hist, 99), (unsigned long)t->missed, (unsigned long)t->exec_cyc.max);
    }
    printf("STAT name=loop n=0 min_us=0 avg_us=0 max_us=0 p99_us=0\r\n");           /* головного циклу немає */
    perf_stat_t s[M_COUNT]; taskENTER_CRITICAL(); memcpy(s, st, sizeof s); taskEXIT_CRITICAL();
    printf("STAT name=btn_latency n=%lu min_us=%lu avg_us=%lu max_us=%lu presses=%lu\r\n", (unsigned long)s[M_BTN].count,
           (unsigned long)cyc2us(s[M_BTN].min), (unsigned long)cyc2us(perf_avg(&s[M_BTN])), (unsigned long)cyc2us(s[M_BTN].max), (unsigned long)g_btn_presses);
    for (int k = M_EVT; k < M_COUNT; k++)
        printf("STAT name=%s n=%lu min_us=%lu avg_us=%lu max_us=%lu\r\n", st_name[k], (unsigned long)s[k].count, (unsigned long)cyc2us(s[k].min),
               (unsigned long)cyc2us(perf_avg(&s[k])), (unsigned long)cyc2us(s[k].max));
    uint32_t now_cyc = platform_cycles();
    taskENTER_CRITICAL(); rts_flush(&g_rts, now_cyc); uint64_t total = rts_total_cyc(&g_rts); uint64_t idl = rts_run_cyc(&g_rts, (uint32_t)xTaskGetIdleTaskHandle()); taskEXIT_CRITICAL();
    uint32_t run_pct = total ? (uint32_t)((total - idl) * 100U / total) : 0;
    printf("LOAD pct=%lu run_pct=%lu btn_lost=%lu uart_rx_overflow=%lu pend_lost=%lu flash_err=%lu imu_err=%lu cam_err=%lu flash_last_rc=%ld\r\n", (unsigned long)last_load_pct,
           (unsigned long)run_pct, (unsigned long)g_btn_lost, (unsigned long)(platform_uart_rx_overflow() + g_rx_lost), (unsigned long)g_pend_lost,
           (unsigned long)g_flash_err, (unsigned long)g_imu_err, (unsigned long)g_cam_err, (long)g_flash_last_rc);
}

void meas_reset_stats(void)
{
    taskENTER_CRITICAL();
    for (int i = 0; i < AO_COUNT; i++) rt_reset_one(&g_rt[i]);
    for (int i = 0; i < M_COUNT; i++) perf_stat_init(&st[i], st_name[i]);
    g_btn_presses = 0; g_btn_lost = 0; g_rx_lost = 0; g_pend_lost = 0; g_ev_lost = 0; g_text_lost = 0; defer_max_used = 0;
    rts_reset(&g_rts, platform_cycles()); win_busy_base = 0; win_cyc0 = platform_cycles();
    taskEXIT_CRITICAL();
    rtos_tick_reset(); rtos_tick_gap_reset(); rtos_switch_gap_reset(); platform_cam_diag_reset();
    printf("[prod] stats reset\r\n");
}

static const char *state_name(eTaskState s)
{
    switch (s) { case eRunning: return "Running"; case eReady: return "Ready"; case eBlocked: return "Blocked"; case eSuspended: return "Suspended"; default: return "Deleted"; }
}

void meas_print_tasks(void)
{
    static TaskStatus_t ts[16];
    UBaseType_t n = uxTaskGetSystemState(ts, 16, NULL);
    uint32_t now_cyc = platform_cycles();
    taskENTER_CRITICAL(); rts_flush(&g_rts, now_cyc); taskEXIT_CRITICAL();
    uint64_t total = rts_total_cyc(&g_rts); uint32_t hz_us = platform_cpu_hz() / 1000000U;
    for (UBaseType_t i = 0; i < n; i++) {
        uint64_t run = rts_run_cyc(&g_rts, (uint32_t)ts[i].xHandle);
        uint32_t pct10 = total ? (uint32_t)(run * 1000U / total) : 0;
        printf("TASK name=%s state=%s prio=%u cpu_us=%lu cpu_pct=%lu.%lu stack_free_words=%u\r\n", ts[i].pcTaskName, state_name(ts[i].eCurrentState),
               (unsigned)ts[i].uxCurrentPriority, (unsigned long)(run / hz_us), (unsigned long)(pct10 / 10), (unsigned long)(pct10 % 10), (unsigned)ts[i].usStackHighWaterMark);
    }
    printf("TASKS total_us=%lu switches=%lu\r\n", (unsigned long)(total / hz_us), (unsigned long)g_rts.total_switches);
}

void meas_print_stack(void)
{
    static TaskStatus_t ts[16];
    UBaseType_t n = uxTaskGetSystemState(ts, 16, NULL);
    for (UBaseType_t i = 0; i < n; i++) {
        unsigned size = 0;
        for (int k = 0; k < AO_COUNT; k++) if (g_rt[k].ao && (TaskHandle_t)g_rt[k].ao->thread == ts[i].xHandle) size = g_rt[k].stack_words;
        if (!size) size = (strcmp(ts[i].pcTaskName, "IDLE") == 0) ? configMINIMAL_STACK_SIZE : 0;
        printf("STACK task=%s size_words=%u free_min_words=%u used_max_bytes=%u\r\n", ts[i].pcTaskName, size, (unsigned)ts[i].usStackHighWaterMark,
               size ? (size - (unsigned)ts[i].usStackHighWaterMark) * 4U : 0U);
    }
    printf("MEM stack_used=%lu stack_reserved=%lu (MSP: обробники переривань)\r\n", (unsigned long)platform_stack_used(), (unsigned long)platform_stack_reserved());
}

void meas_print_heap(void)
{
    printf("HEAP total=%u free=%u min_ever_free=%u used_max=%u malloc_failed=%lu\r\n", (unsigned)configTOTAL_HEAP_SIZE, (unsigned)xPortGetFreeHeapSize(),
           (unsigned)xPortGetMinimumEverFreeHeapSize(), (unsigned)(configTOTAL_HEAP_SIZE - xPortGetMinimumEverFreeHeapSize()), (unsigned long)rtos_malloc_failed_count());
}

void meas_print_oscost(void)
{
    uint32_t n, avg, max; rtos_tick_stats(&n, &avg, &max);
    uint32_t hz = platform_cpu_hz();
    printf("OSCOST tick_hz=%u tick_isr_n=%lu tick_isr_avg_cyc=%lu tick_isr_max_cyc=%lu tick_isr_avg_us_x100=%lu tick_isr_max_us_x100=%lu\r\n",
           (unsigned)configTICK_RATE_HZ, (unsigned long)n, (unsigned long)avg, (unsigned long)max,
           (unsigned long)((uint64_t)avg * 100000000U / hz), (unsigned long)((uint64_t)max * 100000000U / hz));
    uint32_t now_cyc = platform_cycles();
    taskENTER_CRITICAL(); rts_flush(&g_rts, now_cyc); uint64_t win_cyc = rts_total_cyc(&g_rts); taskEXIT_CRITICAL();
    uint32_t win_ms = (uint32_t)(win_cyc / (hz / 1000U));
    uint32_t gap, gaps, off_n, lines, per, resync, sf, ll;
    rtos_tick_gap(&gap, &gaps);
    uint32_t off_max = platform_cam_diag(&off_n, &lines, &per, &resync); platform_cam_loss(&sf, &ll);
    uint32_t swg, swt; rtos_switch_gap(&swg, &swt);
    const char *swn = swt ? pcTaskGetName((TaskHandle_t)swt) : "-";
    printf("OSCOST longest_run_us=%lu longest_run_task=%s\r\n", (unsigned long)swg, swn);
    printf("OSCOST switches=%lu window_ms=%lu tick_gap_max_us=%lu tick_gaps_over_1500us=%lu cam_irq_off_max_us=%lu cam_short_frames=%lu cam_lines_lost=%lu\r\n", (unsigned long)g_rts.total_switches,
           (unsigned long)win_ms, (unsigned long)gap, (unsigned long)gaps, (unsigned long)off_max, (unsigned long)sf, (unsigned long)ll);
}

/* QSTAT (п. 8.4 методички): найбільша заповненість черги кожного AO, запас пулів подій, черга відкладених подій log, втрати */
void meas_print_qstat(void)
{
    for (int i = 0; i < AO_COUNT; i++)
        printf("QUEUE %s max_used=%u/%u\r\n", g_rt[i].name, (unsigned)g_rt[i].q_max, (unsigned)g_rt[i].queue_len);
    printf("DEFER log max_used=%u/%u\r\n", (unsigned)defer_max_used, (unsigned)defer_len);
    for (uint_fast8_t p = 1; p <= QF_MAX_EPOOL; p++)
        printf("POOL %u min_free=%u/%u\r\n", (unsigned)p, (unsigned)QF_getPoolMin(p), (unsigned)(QF_getPoolFree(p) + QF_getPoolUse(p)));
    printf("LOST lines=%lu logs=%lu events=%lu texts=%lu\r\n", (unsigned long)g_rx_lost, (unsigned long)g_pend_lost, (unsigned long)g_ev_lost, (unsigned long)g_text_lost);
}

void meas_print_mem(void)
{
    printf("MEM stack_used=%lu stack_reserved=%lu heap_arena=%lu frame_buf=%u\r\n", (unsigned long)platform_stack_used(),
           (unsigned long)platform_stack_reserved(), (unsigned long)platform_heap_used(), (unsigned)FRAME_PIXELS);
}

#ifndef GIT_HASH
#define GIT_HASH "dev"
#endif
void meas_print_version(void)
{
    printf("VER git=%s build=\"%s %s\" sysclk=%lu task_tick_us=1 os=FreeRTOS-%s tick_hz=%u fw=QP/C-%s\r\n", GIT_HASH, __DATE__, __TIME__, (unsigned long)platform_cpu_hz(),
           tskKERNEL_VERSION_NUMBER, (unsigned)configTICK_RATE_HZ, QP_VERSION_STR);
}

void meas_trace_cmd(int argc, char **argv)
{
    if (argc >= 1 && !strcmp(argv[0], "ARM")) { taskENTER_CRITICAL(); rts_trace_arm(&g_rts); taskEXIT_CRITICAL(); printf("[trace] armed (%u switches)\r\n", (unsigned)RTS_TRACE_N); return; }
    uint32_t cnt = g_rts.ev_count, lim = 0;
    if (argc >= 2) { for (const char *s = argv[1]; *s >= '0' && *s <= '9'; s++) lim = lim * 10 + (uint32_t)(*s - '0'); }
    if (lim && lim < cnt) cnt = lim;
    printf("[trace] %lu events, t in us from first\r\n", (unsigned long)cnt);
    uint32_t t0 = cnt ? g_rts.ev[0].cyc : 0, hz_us = platform_cpu_hz() / 1000000U;
    for (uint32_t i = 0; i < cnt; i++) {
        const char *nm = pcTaskGetName((TaskHandle_t)g_rts.ev[i].id);
        printf("TRACE t_us=%lu task=%s\r\n", (unsigned long)((g_rts.ev[i].cyc - t0) / hz_us), nm ? nm : "?");
    }
}

void meas_cmd_started(void) { meas_add(M_CMD, platform_cycles() - platform_uart_eol_cycles()); }   /* Enter у ISR -> початок виконання команди */

void meas_probe_test(void)
{
    uint32_t hz_us = platform_cpu_hz() / 1000000U;
    uint32_t t0 = platform_cycles(), next[PLATFORM_PROBE_COUNT];
    int lvl[PLATFORM_PROBE_COUNT] = {0};
    for (int k = 0; k < PLATFORM_PROBE_COUNT; k++) next[k] = 0;
    while (platform_cycles() - t0 < 4000000U * hz_us) {
        uint32_t el = (platform_cycles() - t0) / hz_us;
        for (int k = 0; k < PLATFORM_PROBE_COUNT; k++) {
            if (el >= next[k]) { lvl[k] ^= 1; platform_probe(k, lvl[k]); next[k] = el + (100U << k); }
        }
    }
    for (int k = 0; k < PLATFORM_PROBE_COUNT; k++) platform_probe(k, 0);
}
