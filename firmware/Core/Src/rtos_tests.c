/** @file rtos_tests.c — навмисні поломки і вимірювання вартості ОС (ЛР4, п. 6.5 і 7.2). Викликаються командою TEST з CLI. */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "platform.h"
#include "rtos_tests.h"

static uint32_t parse(const char *s, uint32_t def)
{
    if (!s || !*s) return def;
    uint32_t v = 0;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (uint32_t)(*s - '0');
    return v;
}

/* Активне очікування (вантаження CPU), яке можуть витіснити */
static void busy_us(uint32_t us)
{
    uint32_t t0 = platform_cycles(), c = us * (platform_cpu_hz() / 1000000U);
    while (platform_cycles() - t0 < c) { }
}
static uint32_t elapsed_us(uint32_t t0) { return (platform_cycles() - t0) / (platform_cpu_hz() / 1000000U); }

/* ---- 1. інверсія пріоритетів: low тримає ресурс, high його чекає, mid займає процесор ---- */
static SemaphoreHandle_t inv_lock;
static volatile uint32_t inv_wait_us, inv_done;
static uint32_t inv_low_ms, inv_mid_ms;

static void inv_low(void *a)  { (void)a; xSemaphoreTake(inv_lock, portMAX_DELAY); busy_us(inv_low_ms * 1000U); xSemaphoreGive(inv_lock); inv_done |= 1; vTaskDelete(NULL); }
static void inv_mid(void *a)  { (void)a; vTaskDelay(pdMS_TO_TICKS(30)); busy_us(inv_mid_ms * 1000U); inv_done |= 2; vTaskDelete(NULL); }
static void inv_high(void *a) { (void)a; vTaskDelay(pdMS_TO_TICKS(20)); uint32_t t0 = platform_cycles(); xSemaphoreTake(inv_lock, portMAX_DELAY);
                                inv_wait_us = elapsed_us(t0); xSemaphoreGive(inv_lock); inv_done |= 4; vTaskDelete(NULL); }

static int test_inv(int argc, char **argv)
{
    int mutex = !(argc > 1 && !strcmp(argv[1], "SEM"));
    inv_low_ms = parse(argc > 2 ? argv[2] : 0, 200); inv_mid_ms = parse(argc > 3 ? argv[3] : 0, 300);
    inv_lock = mutex ? xSemaphoreCreateMutex() : xSemaphoreCreateBinary();
    if (!inv_lock) return -1;
    if (!mutex) xSemaphoreGive(inv_lock);                  /* двійковий семафор: початково «вільний», без успадкування пріоритету */
    inv_done = 0; inv_wait_us = 0;
    xTaskCreate(inv_low,  "inv_low",  256, NULL, 1, NULL);
    xTaskCreate(inv_mid,  "inv_mid",  256, NULL, 3, NULL);
    xTaskCreate(inv_high, "inv_high", 256, NULL, 5, NULL);
    for (int i = 0; i < 400 && inv_done != 7; i++) vTaskDelay(pdMS_TO_TICKS(10));
    printf("[test] INV mode=%s low_ms=%lu mid_ms=%lu high_wait_us=%lu done=%lu\r\n", mutex ? "mutex" : "binary_sem", (unsigned long)inv_low_ms,
           (unsigned long)inv_mid_ms, (unsigned long)inv_wait_us, (unsigned long)inv_done);
    vSemaphoreDelete(inv_lock);
    return 0;
}

/* ---- 2. переповнення стека ---- */
/* Задача зі стеком 96 слів (384 Б) рекурсивно займає ~64 Б на рівень. На найглибшому рівні вона віддає процесор (vTaskDelay):
 * саме при перемиканні контексту ядро перевіряє стек (configCHECK_FOR_STACK_OVERFLOW = 2) і викликає хук. Перевищення невелике
 * (≈ 100–200 Б), щоб встигнути побачити реакцію ядра до того, як зіпсовані сусідні блоки heap обвалять програму. */
static uint32_t stk_depth;
static void recurse(uint32_t n)
{
    volatile uint8_t buf[48];
    buf[0] = (uint8_t)n;
    if (n) recurse(n - 1); else vTaskDelay(pdMS_TO_TICKS(5));
    buf[1] = buf[0];
}
static void stk_task(void *a) { (void)a; recurse(stk_depth); printf("[test] STK: рекурсія завершилась без реакції ядра\r\n"); for (;;) vTaskDelay(1000); }

static int test_stk(int argc, char **argv)
{
    stk_depth = parse(argc > 1 ? argv[1] : 0, 8);
    printf("[test] STK: стек 96 слів, глибина рекурсії %lu (~%lu Б); очікуємо FATAL stack overflow і м'який перезапуск\r\n", (unsigned long)stk_depth, (unsigned long)(stk_depth * 64U + 100U));
    xTaskCreate(stk_task, "stkovf", 96, NULL, 3, NULL);
    vTaskDelay(pdMS_TO_TICKS(3000));
    printf("[test] STK: хук не спрацював за 3 с (контроль вимкнено або збій інший)\r\n");
    return 0;
}

/* ---- 3. голодування: задача вищого пріоритету не блокується ---- */
static uint32_t starve_ms;
static void starve_task(void *a)
{
    (void)a;
    uint32_t t0 = platform_cycles();
    while (elapsed_us(t0) < starve_ms * 1000U) { }              /* активне очікування прапорця: без vTaskDelay і без блокування */
    printf("[test] STARVE %lu ms done\r\n", (unsigned long)starve_ms);
    vTaskDelete(NULL);
}
static int test_starve(int argc, char **argv)
{
    starve_ms = parse(argc > 1 ? argv[1] : 0, 1000);
    printf("[test] STARVE start: задача пріоритету 7 не блокується %lu мс\r\n", (unsigned long)starve_ms);
    xTaskCreate(starve_task, "hog", 256, NULL, 7, NULL);
    return 0;
}

/* ---- 4. вичерпання черги: споживач повільніший за виробника ---- */
static QueueHandle_t qf;
static volatile uint32_t qf_consumed;
static void qf_cons(void *a)
{
    (void)a; uint32_t x;
    while (xQueueReceive(qf, &x, pdMS_TO_TICKS(500)) == pdTRUE) { busy_us(5000); qf_consumed++; }     /* 5 мс на елемент = 200/с */
    vTaskDelete(NULL);
}
static int test_qflood(int argc, char **argv)
{
    uint32_t n = parse(argc > 1 ? argv[1] : 0, 200), to_ms = parse(argc > 2 ? argv[2] : 0, 0);
    qf = xQueueCreate(8, sizeof(uint32_t)); if (!qf) return -1;
    qf_consumed = 0;
    xTaskCreate(qf_cons, "qcons", 256, NULL, 2, NULL);
    uint32_t sent = 0, lost = 0, t0 = platform_cycles();
    for (uint32_t i = 0; i < n; i++) {
        if (xQueueSend(qf, &i, pdMS_TO_TICKS(to_ms)) == pdTRUE) sent++; else lost++;
    }
    uint32_t blocked_us = elapsed_us(t0);
    vTaskDelay(pdMS_TO_TICKS(1500));
    printf("[test] QFLOOD n=%lu timeout_ms=%lu sent=%lu lost=%lu consumed=%lu producer_time_us=%lu\r\n", (unsigned long)n, (unsigned long)to_ms,
           (unsigned long)sent, (unsigned long)lost, (unsigned long)qf_consumed, (unsigned long)blocked_us);
    vQueueDelete(qf);
    return 0;
}

/* ---- 5. вичерпання heap ОС ---- */
static int test_heap(int argc, char **argv)
{
    (void)argc; (void)argv;
    static void *blk[64];
    int n = 0;
    size_t free0 = xPortGetFreeHeapSize();
    while (n < 64 && (blk[n] = pvPortMalloc(1024)) != NULL) n++;
    printf("[test] HEAP free_before=%u blocks_1k=%d (null at block %d)\r\n", (unsigned)free0, n, n + 1);
    for (int i = 0; i < n; i++) vPortFree(blk[i]);
    printf("[test] HEAP free_after=%u\r\n", (unsigned)xPortGetFreeHeapSize());
    return 0;
}

/* ---- 6. затримка перемикання контексту: від xTaskNotifyGive у слабшій задачі до першої інструкції в сильнішій ---- */
static TaskHandle_t ctx_hi; static volatile uint32_t ctx_t1; static volatile int ctx_run;
static void ctx_hi_task(void *a)
{
    (void)a;
    while (ctx_run) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); ctx_t1 = platform_cycles(); }
    vTaskDelete(NULL);
}
static int test_ctx(int argc, char **argv)
{
    uint32_t n = parse(argc > 1 ? argv[1] : 0, 1000), mn = 0xFFFFFFFFu, mx = 0; uint64_t sum = 0;
    ctx_run = 1;
    xTaskCreate(ctx_hi_task, "ctxhi", 192, NULL, 7, &ctx_hi);
    vTaskDelay(pdMS_TO_TICKS(5));
    for (uint32_t i = 0; i < n; i++) {
        uint32_t t0 = platform_cycles();
        xTaskNotifyGive(ctx_hi);
        uint32_t d = ctx_t1 - t0;
        sum += d; if (d < mn) mn = d; if (d > mx) mx = d;
        vTaskDelay(1);
    }
    ctx_run = 0; xTaskNotifyGive(ctx_hi);
    uint32_t hz = platform_cpu_hz();
    printf("[test] CTX n=%lu notify_to_wake min_cyc=%lu avg_cyc=%lu max_cyc=%lu min_ns=%lu avg_ns=%lu max_ns=%lu\r\n", (unsigned long)n, (unsigned long)mn,
           (unsigned long)(sum / n), (unsigned long)mx, (unsigned long)((uint64_t)mn * 1000000000U / hz), (unsigned long)((sum / n) * 1000000000U / hz),
           (unsigned long)((uint64_t)mx * 1000000000U / hz));
    return 0;
}

/* ---- 7. діагностика захоплення кадру з увімкненими перериваннями: вікна IRQ-off і втрата тіків ---- */
#include "rtos_port.h"
extern void product_snapshot_request(void);
static int test_camdiag(int argc, char **argv)
{
    uint32_t ms = parse(argc > 1 ? argv[1] : 0, 4000);
    uint32_t n0, a0, m0, n1, a1, m1;
    platform_cam_diag_reset(); rtos_tick_gap_reset();
    rtos_tick_stats(&n0, &a0, &m0);
    uint32_t t0 = platform_cycles();
    product_snapshot_request();
    vTaskDelay(pdMS_TO_TICKS(ms));
    uint32_t el_ms = (platform_cycles() - t0) / (platform_cpu_hz() / 1000U);
    rtos_tick_stats(&n1, &a1, &m1);
    uint32_t off_n, lines, per, resync, off_max = platform_cam_diag(&off_n, &lines, &per, &resync), gap, gaps;
    rtos_tick_gap(&gap, &gaps);
    uint32_t sf, ll; platform_cam_loss(&sf, &ll);
    printf("[test] CAMDIAG window_ms=%lu ticks=%lu irq_off_windows=%lu irq_off_max_us=%lu last_line_period_us=%lu lines=%lu late_open=%lu tick_gap_max_us=%lu tick_gaps_over_1500us=%lu short_frames=%lu lines_lost=%lu\r\n",
           (unsigned long)el_ms, (unsigned long)(n1 - n0), (unsigned long)off_n, (unsigned long)off_max, (unsigned long)per, (unsigned long)lines, (unsigned long)resync, (unsigned long)gap, (unsigned long)gaps, (unsigned long)sf, (unsigned long)ll);
    return 0;
}

int rtos_test_run(int argc, char **argv)
{
    if (argc < 1) return -1;
    if (!strcmp(argv[0], "INV"))    return test_inv(argc, argv);
    if (!strcmp(argv[0], "STK"))    return test_stk(argc, argv);
    if (!strcmp(argv[0], "STARVE")) return test_starve(argc, argv);
    if (!strcmp(argv[0], "QFLOOD")) return test_qflood(argc, argv);
    if (!strcmp(argv[0], "HEAP"))   return test_heap(argc, argv);
    if (!strcmp(argv[0], "CTX"))    return test_ctx(argc, argv);
    if (!strcmp(argv[0], "CAMDIAG")) return test_camdiag(argc, argv);
    return -1;
}
