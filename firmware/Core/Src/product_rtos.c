/**
 * product_rtos.c — продукт ЛР4: ті самі функції, що в ЛР3 (product.c у тезі lab3), але виконання організовано задачами FreeRTOS.
 * Розбиття на задачі, пріоритети і механізми взаємодії — docs/rtos-design.md. Лише цей файл і rtos_port.c знають про ядро:
 * логіка (трекер, клас неба, журнал, кадри, калібровка) — ті самі модулі App/, що й у ЛР3.
 */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "product.h"
#include "cli.h"
#include "platform.h"
#include "perf.h"
#include "evlog.h"
#include "framestore.h"
#include "calib.h"
#include "mpu6050.h"
#include "ov7670.h"
#include "w25q.h"
#include "quadrant.h"
#include "sky.h"
#include "rtstats.h"
#include "rtos_port.h"
#include "rtos_tests.h"

/* ---- пріоритети (чим жорсткіший дедлайн, тим вищий; довгі операції — найнижчі) ---- */
enum { PRIO_CAM = 1, PRIO_SNAP = 1, PRIO_LED = 1, PRIO_LOG = 2, PRIO_LOSS = 2, PRIO_IMU = 2, PRIO_TICK = 2,
       PRIO_CLI = 3, PRIO_CONTROL = 4, PRIO_SERVO = 4, PRIO_ADC = 5, PRIO_BUTTON = 6 };

/* ---- стан (той самий, що в ЛР3) ---- */
static w25q_t   flash;
static int      flash_ok;
static evlog_t  elog;
static tracker_t trk;
static loss_t   loss;
static uint32_t frame_next = 1;
static uint8_t  g_frame[FRAME_PIXELS];
static int      have_frame;
static frame_stats_t fst;
static volatile sky_t sky = SKY_OVERCAST;
static volatile int   sky_force = -1;
static uint16_t q[4];
static volatile int servos_on;
static uint32_t last_snap_ms, last_suspect_ms;
static volatile uint32_t btn_presses, btn_lost, rx_lost;
static volatile uint32_t flash_err, imu_err, cam_err;
static volatile int32_t flash_last_rc;          /* код останньої помилки flash (DEV_ERR_*), для розбору таких випадків */
static mpu6050_t imu;
static mpu6050_raw_t imu_raw;
static uint32_t seconds;

/* ---- об'єкти ядра ---- */
static SemaphoreHandle_t    flash_mx;     /* SPI2/W25Q64: журнал, кадри, калібровка (успадкування пріоритету) */
static SemaphoreHandle_t    frame_mx;     /* спільний буфер кадру 19 200 Б */
static QueueHandle_t        q_log;        /* записи журналу від задач -> задача log */
static QueueHandle_t        q_btn;        /* мітки DWT подій кнопки: ISR -> задача button */
static StreamBufferHandle_t sb_rx;        /* байти UART RX: ISR -> задача cli */
static TaskHandle_t         h_snap;

typedef struct { uint8_t type, frame; uint32_t t_ms; int16_t a, b, c; } pend_t;
typedef struct { uint32_t isr, evt; } btn_evt_t;      /* вхід у ISR і справжній момент події (DWT) */
static volatile uint32_t pend_lost;

static void log_event(uint8_t type, int16_t a, int16_t b, int16_t c)
{
    pend_t p = { type, EVLOG_NO_FRAME, platform_millis(), a, b, c };
    if (xQueueSend(q_log, &p, 0) != pdTRUE) pend_lost++;          /* черга повна: подію втрачено, але видно в лічильнику */
}

static void snap_request(void) { if (h_snap) xTaskNotifyGive(h_snap); }

/* ---- статистика задач: формат STAT як у ЛР3 ---- */
typedef struct {
    const char    *name;
    uint16_t       stack_words;
    UBaseType_t    prio;
    volatile uint32_t period_ms;         /* 0 — подієва задача */
    volatile uint8_t  rebase;            /* PERIOD змінено: перервати сон і почати відлік наново */
    TaskHandle_t   h;
    uint32_t       runs, missed;
    perf_stat_t    exec_cyc;             /* від пробудження до наступного очікування; ВКЛЮЧАЄ час, коли задачу витісняли */
    perf_stat_t    late_us;              /* запізнення пробудження відносно плану (джитер) */
    perf_hist_t    late_hist;
} rt_t;
static rt_t rt[12];
static int  nrt;
static perf_stat_t st_btn, st_evt, st_cam_frame, st_cmd;
static uint32_t stats_t0_us, last_load_pct; static uint64_t win_busy_base; static uint32_t win_cyc0;

static void rt_reset_one(rt_t *r)
{
    r->runs = 0; r->missed = 0;
    perf_stat_init(&r->exec_cyc, r->name); perf_stat_init(&r->late_us, r->name); perf_hist_init(&r->late_hist, 100);
}

static void rt_account(rt_t *r, uint32_t t0_cyc)
{
    taskENTER_CRITICAL();
    perf_add(&r->exec_cyc, platform_cycles() - t0_cyc);
    r->runs++;
    taskEXIT_CRITICAL();
}

static void rt_late(rt_t *r, uint32_t late_us, uint32_t skipped)
{
    taskENTER_CRITICAL();
    perf_add(&r->late_us, late_us); perf_hist_add(&r->late_hist, late_us);
    r->missed += skipped;
    taskEXIT_CRITICAL();
}

/* Періодична задача: vTaskDelayUntil (без накопичення похибки). Джитер = час від ІДЕАЛЬНОГО моменту потрібного тіка (SysTick->VAL при вході в
 * ISR віднімається, тож затримка тіка через вимкнені IRQ теж враховується) до моменту, коли задача реально почала працювати. */
static void periodic(rt_t *r, void (*work)(void))
{
    uint32_t cyc_us = platform_cpu_hz() / 1000000U;
    vTaskDelay(1);                                         /* синхронізація з межею тіка */
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(r->period_ms));    /* після повернення last = тік, на який планувалося пробудження */
        if (r->rebase) { r->rebase = 0; last = xTaskGetTickCount(); continue; }   /* PERIOD змінили: сон перервано, відлік з нуля, цей прохід не рахується */
        uint32_t t0 = platform_cycles();
        int32_t late = (int32_t)(t0 - rtos_tick_time(last));
        uint32_t late_us = late > 0 ? (uint32_t)late / cyc_us : 0U, skipped = 0, per_us = r->period_ms * 1000U;
        /* Та сама семантика, що в ЛР3 (sched.c): повністю пропущені періоди рахуються в missed і не наздоганяються пачкою,
         * джитер — залишок запізнення в межах одного періоду. */
        if (late_us >= per_us) { skipped = late_us / per_us; late_us -= skipped * per_us; last += skipped * pdMS_TO_TICKS(r->period_ms); }
        rt_late(r, late_us, skipped);
        work();
        rt_account(r, t0);
    }
}

/* ================= задачі ================= */
static void adc_work(void)
{
    uint16_t t[4];
    platform_probe(0, 1);
    platform_adc_read_quadrant(t);
    platform_probe(0, 0);
    taskENTER_CRITICAL(); memcpy(q, t, sizeof q); taskEXIT_CRITICAL();
}

static void control_work(void)
{
    uint16_t t[4];
    uint32_t now = platform_millis();
    taskENTER_CRITICAL(); memcpy(t, q, sizeof t); taskEXIT_CRITICAL();
    taskENTER_CRITICAL();
    track_mode_t before = trk.mode;
    int changed = tracker_update(&trk, t, sky, now);
    track_mode_t after = trk.mode; uint32_t light = trk.light;
    taskEXIT_CRITICAL();
    if (changed) {
        log_event(EV_MODE, (int16_t)before, (int16_t)after, (int16_t)(light / 4));
        if (after == MODE_PARK) snap_request();                          /* FR-14: вхід у PARK = знімок */
    }
}

static void servo_work(void)
{
    if (!servos_on) return;
    uint32_t a = servo_next_step(platform_servo_get_us(PLATFORM_SERVO_AZ), trk.az_us, 20);
    uint32_t e = servo_next_step(platform_servo_get_us(PLATFORM_SERVO_EL), trk.el_us, 20);
    platform_servo_set_us(PLATFORM_SERVO_AZ, a);
    platform_servo_set_us(PLATFORM_SERVO_EL, e);
}

static void led_work(void)
{
    static uint8_t n;
    n++;
    if (trk.mode == MODE_TRACK) platform_led_toggle();
    else if (trk.mode == MODE_OVERCAST) { if (n % 5 == 0) platform_led_toggle(); }
    else platform_marker((n % 20) == 0);
}

static void imu_work(void)
{
    if (mpu6050_read_accel_raw(&imu, &imu_raw) != DEV_OK) imu_err++;
}

static void loss_work(void)
{
    if (loss_update(&loss, trk.light, platform_millis())) { log_event(EV_SIGNAL_LOSS, (int16_t)(trk.light / 4), 0, 0); snap_request(); }
}

static void camera_work(void)
{
    xSemaphoreTake(frame_mx, portMAX_DELAY);
    platform_probe(2, 1);
    uint32_t c0 = platform_cycles();
    int rc = platform_cam_capture(g_frame, FRAME_PIXELS, 0, 1500);
    uint32_t cam_cyc = platform_cycles() - c0;
    platform_probe(2, 0);
    if (rc != 0) { cam_err++; xSemaphoreGive(frame_mx); return; }
    taskENTER_CRITICAL(); perf_add(&st_cam_frame, cam_cyc); taskEXIT_CRITICAL();
    have_frame = 1;
    fst = frame_stats(g_frame, FRAME_PIXELS, SKY_DEFAULTS.sat_level);
    xSemaphoreGive(frame_mx);
    sky = sky_force >= 0 ? (sky_t)sky_force : sky_classify(&fst, &SKY_DEFAULTS);
    int ldr_dark = trk.light < trk.cfg.dark_light_thr;
    uint32_t now = platform_millis();
    if (sky_mismatch(sky, ldr_dark) && now - last_suspect_ms >= 10000U) {
        last_suspect_ms = now;
        log_event(EV_SENSOR_SUSPECT, fst.mean, (int16_t)(trk.light / 4), (int16_t)sky);
    }
}

static void tick_work(void)
{
    seconds++;
    uint32_t now_cyc = platform_cycles();
    taskENTER_CRITICAL(); rts_flush(&g_rts, now_cyc); uint64_t total = rts_total_cyc(&g_rts); uint64_t idl = rts_run_cyc(&g_rts, (uint32_t)xTaskGetIdleTaskHandle()); taskEXIT_CRITICAL();
    uint64_t busy = (total - idl) - win_busy_base; uint32_t window = now_cyc - win_cyc0;
    last_load_pct = window ? (uint32_t)(busy * 100U / window) : 0;
    win_busy_base = total - idl; win_cyc0 = now_cyc;
    uint16_t t[4]; taskENTER_CRITICAL(); memcpy(t, q, sizeof t); taskEXIT_CRITICAL();
    quadrant_error_t e = quadrant_error(t);
    printf("tick %lu (uptime %lu s, TL=%u TR=%u BL=%u BR=%u, ex=%ld ey=%ld) mode=%s sky=%s load=%lu%%\r\n",
           (unsigned long)platform_millis(), (unsigned long)seconds, t[0], t[1], t[2], t[3],
           (long)e.x_permille, (long)e.y_permille, tracker_mode_name(trk.mode), sky_name(sky), (unsigned long)last_load_pct);
}

/* ---- подієві задачі ---- */
static void snapshot_task(void *arg)
{
    rt_t *r = arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);               /* спимо до події (кнопка, втрата сигналу, SNAP) */
        uint32_t t0 = platform_cycles(), now = platform_millis();
        if (!flash_ok || now - last_snap_ms < 3000U) { rt_account(r, t0); continue; }   /* FR-11: не частіше ніж раз на 3 с */
        last_snap_ms = now;
        xSemaphoreTake(frame_mx, portMAX_DELAY);
        platform_probe(3, 1);
        if (platform_cam_capture(g_frame, FRAME_PIXELS, 0, 1500) != 0) { cam_err++; platform_probe(3, 0); xSemaphoreGive(frame_mx); rt_account(r, t0); continue; }
        have_frame = 1;
        uint32_t no = frame_next;
        xSemaphoreTake(flash_mx, portMAX_DELAY);
        int rc = framestore_save(&flash, no, g_frame, FRAME_PIXELS);        /* стирання 5 секторів + запис 19 КБ */
        if (rc == DEV_OK) {
            frame_next++;
            rc = evlog_append(&elog, EV_SIGNAL_LOSS, (uint8_t)(no & 0xFF), now, (int16_t)(trk.light / 4), (int16_t)trk.mode, fst.mean);
        }
        xSemaphoreGive(flash_mx);
        if (rc != DEV_OK) { flash_err++; flash_last_rc = rc; }
        platform_probe(3, 0);
        xSemaphoreGive(frame_mx);
        rt_account(r, t0);
    }
}

static void log_task(void *arg)
{
    rt_t *r = arg; pend_t p;
    for (;;) {
        if (xQueueReceive(q_log, &p, portMAX_DELAY) != pdTRUE) continue;
        uint32_t t0 = platform_cycles();
        if (flash_ok) {
            xSemaphoreTake(flash_mx, portMAX_DELAY);
            int rc = evlog_append(&elog, p.type, p.frame, p.t_ms, p.a, p.b, p.c);
            if (rc != DEV_OK) { flash_err++; flash_last_rc = rc; }
            xSemaphoreGive(flash_mx);
        }
        rt_account(r, t0);
    }
}

static void button_task(void *arg)
{
    rt_t *r = arg; btn_evt_t ev;
    for (;;) {
        if (xQueueReceive(q_btn, &ev, portMAX_DELAY) != pdTRUE) continue;
        uint32_t t0 = platform_cycles();
        platform_probe(4, 1);                                           /* початок реакції (аналізатор) */
        taskENTER_CRITICAL(); perf_add(&st_btn, t0 - ev.isr); perf_add(&st_evt, t0 - ev.evt); taskEXIT_CRITICAL();   /* ISR -> початок реакції; подія -> початок реакції */
        btn_presses++;
        snap_request();
        platform_probe(4, 0);
        rt_account(r, t0);
    }
}

static void cli_task(void *arg)
{
    rt_t *r = arg; uint8_t c; uint32_t last_rx_ms = 0;
    for (;;) {
        if (xStreamBufferReceive(sb_rx, &c, 1, pdMS_TO_TICKS(100)) == 1) {
            uint32_t t0 = platform_cycles();
            cli_feed((char)c);
            last_rx_ms = platform_millis();
            rt_account(r, t0);
        } else if (last_rx_ms && platform_millis() - last_rx_ms > 2000U) { cli_reset(); last_rx_ms = 0; }
    }
}

static void btn_isr_hook(uint32_t isr_cyc, uint32_t evt_cyc)
{
    BaseType_t w = pdFALSE;
    btn_evt_t ev = { isr_cyc, evt_cyc };
    if (xQueueSendFromISR(q_btn, &ev, &w) != pdTRUE) btn_lost++;      /* черга переповнилась */
    portYIELD_FROM_ISR(w);
}
static void uart_isr_hook(uint8_t b)
{
    BaseType_t w = pdFALSE;
    if (xStreamBufferSendFromISR(sb_rx, &b, 1, &w) != 1) rx_lost++;
    portYIELD_FROM_ISR(w);
}
static void catchup_hook(uint32_t ms) { xTaskCatchUpTicks(ms); }
/* Очікування в драйверах (опитування статусу flash, SCCB): до старту планувальника — HAL_Delay, далі задача блокується і віддає процесор */
static void rtos_delay_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); else HAL_Delay(ms);
}

/* ---- створення задач ---- */
typedef struct { const char *name; uint16_t stack; UBaseType_t prio; uint32_t period_ms; void (*work)(void); TaskFunction_t ev; } spec_t;
static void periodic_entry(void *arg);
static void (*work_of[12])(void);

static void periodic_entry(void *arg) { rt_t *r = arg; periodic(r, work_of[r - rt]); }

#define SPEC(nm, st, pr, per, w, evf) { nm, st, pr, per, w, evf }
#ifndef RTOS_STACK_SCALE_PCT
#define RTOS_STACK_SCALE_PCT 100
#endif
static void spawn(const spec_t *s)
{
    rt_t *r = &rt[nrt];
    r->name = s->name; r->stack_words = (uint16_t)((uint32_t)s->stack * RTOS_STACK_SCALE_PCT / 100U); r->prio = s->prio; r->period_ms = s->period_ms;
    rt_reset_one(r);
    work_of[nrt] = s->work;
    BaseType_t rc = xTaskCreate(s->ev ? s->ev : periodic_entry, s->name, r->stack_words, r, s->prio, &r->h);
    configASSERT(rc == pdPASS);
    nrt++;
}

/* ================= ініціалізація ================= */
void product_init(void)
{
    w25q_init_dev(&flash, platform_spi2_flash());
    uint8_t id[3];
    w25q_wake(&flash);
    flash_ok = w25q_read_jedec(&flash, id) == DEV_OK;
    tracker_cfg_t cfg; tracker_default_cfg(&cfg);
    if (flash_ok) {
        if (calib_load(&flash, &cfg) != DEV_OK) printf("[prod] calibration: defaults (flash empty)\r\n");
        uint32_t c0 = platform_cycles();
        if (evlog_open(&elog, &flash) != DEV_OK) flash_ok = 0;
        framestore_scan(&flash, &frame_next);
        printf("[prod] log: %lu records, next frame #%lu, open %lu ms\r\n", (unsigned long)elog.valid, (unsigned long)frame_next,
               (unsigned long)((platform_cycles() - c0) / (platform_cpu_hz() / 1000U)));
    }
    tracker_init(&trk, &cfg);
    loss_init(&loss);
    mpu6050_init_dev(&imu, platform_i2c1());
    mpu6050_wake(&imu);
    { ov7670_t cam; ov7670_init_dev(&cam, platform_sccb_cam()); if (ov7670_config_qqvga_yuv(&cam) != DEV_OK) cam_err++; }
    platform_adc_read_quadrant(q);
    perf_stat_init(&st_btn, "btn_latency"); perf_stat_init(&st_evt, "evt_latency"); perf_stat_init(&st_cam_frame, "cam_frame"); perf_stat_init(&st_cmd, "cmd_latency");

    rtos_print_init();
    flash_mx = xSemaphoreCreateMutex(); frame_mx = xSemaphoreCreateMutex();
    q_log = xQueueCreate(16, sizeof(pend_t)); q_btn = xQueueCreate(16, sizeof(btn_evt_t));
    sb_rx = xStreamBufferCreate(256, 1);
    configASSERT(flash_mx && frame_mx && q_log && q_btn && sb_rx);

    /* таблиця задач: ті самі імена, що в ЛР3 (скрипти аналізу читають STAT name=...) */
    static const spec_t specs[] = {
        SPEC("button",   112, PRIO_BUTTON,  0,    0,          button_task),
        SPEC("cli",      320, PRIO_CLI,     0,    0,          cli_task),
        SPEC("adc",      112, PRIO_ADC,     10,   adc_work,   0),
        SPEC("control",  112, PRIO_CONTROL, 10,   control_work, 0),
        SPEC("servo",     96, PRIO_SERVO,   20,   servo_work, 0),
        SPEC("loss",      96, PRIO_LOSS,    100,  loss_work,  0),
        SPEC("imu",      224, PRIO_IMU,     100,  imu_work,   0),
        SPEC("led",       96, PRIO_LED,     100,  led_work,   0),
        SPEC("log",      256, PRIO_LOG,     0,    0,          log_task),
        SPEC("snapshot", 288, PRIO_SNAP,    0,    0,          snapshot_task),
        SPEC("camera",   224, PRIO_CAM,     3000, camera_work, 0),
        SPEC("tick",     320, PRIO_TICK,    1000, tick_work,  0),
    };
    for (unsigned i = 0; i < sizeof specs / sizeof specs[0]; i++) spawn(&specs[i]);
    h_snap = rt[9].h;

    platform_set_button_hook(btn_isr_hook);
    platform_set_uart_hook(uart_isr_hook);
    platform_cam_set_irq_mode(1);                       /* ЛР4: IRQ вимкнені лише на активну частину рядка */
    platform_cam_set_catchup_hook(catchup_hook);
    platform_set_delay_hook(rtos_delay_ms);
    rts_init(&g_rts, platform_cycles());
    stats_t0_us = platform_micros(); win_cyc0 = platform_cycles();
    log_event(EV_BOOT, 0, 0, 0);
    printf("[prod] %u tasks, tick=%u Hz, heap=%u B, boot_ms=%lu\r\n", (unsigned)nrt, (unsigned)configTICK_RATE_HZ, (unsigned)configTOTAL_HEAP_SIZE, (unsigned long)platform_millis());
    product_print_version();
}

void product_start(void)
{
    vTaskStartScheduler();
    for (;;) { }
}

/* ================= діагностика ================= */
static uint32_t cyc2us(uint32_t c) { return c / (platform_cpu_hz() / 1000000U); }

static void snap_stats(rt_t *dst)    /* копія під критичною секцією: задачі пишуть у свої лічильники в іншому контексті */
{
    taskENTER_CRITICAL(); memcpy(dst, rt, sizeof(rt_t) * (size_t)nrt); taskEXIT_CRITICAL();
}

void product_print_stats(void)
{
    static rt_t c[12];
    snap_stats(c);
    for (int i = 0; i < nrt; i++) {
        rt_t *t = &c[i];
        printf("STAT name=%s n=%lu min_us=%lu avg_us=%lu max_us=%lu late_max_us=%lu late_p99_us=%lu missed=%lu max_cyc=%lu\r\n",
               t->name, (unsigned long)t->runs, (unsigned long)cyc2us(t->exec_cyc.min), (unsigned long)cyc2us(perf_avg(&t->exec_cyc)),
               (unsigned long)cyc2us(t->exec_cyc.max), (unsigned long)t->late_us.max,
               (unsigned long)perf_hist_percentile(&t->late_hist, 99), (unsigned long)t->missed, (unsigned long)t->exec_cyc.max);
    }
    printf("STAT name=loop n=0 min_us=0 avg_us=0 max_us=0 p99_us=0\r\n");           /* головного циклу в ОС немає */
    perf_stat_t b, f, ev; taskENTER_CRITICAL(); b = st_btn; f = st_cam_frame; ev = st_evt; taskEXIT_CRITICAL();
    printf("STAT name=btn_latency n=%lu min_us=%lu avg_us=%lu max_us=%lu presses=%lu\r\n", (unsigned long)b.count,
           (unsigned long)cyc2us(b.min), (unsigned long)cyc2us(perf_avg(&b)), (unsigned long)cyc2us(b.max), (unsigned long)btn_presses);
    printf("STAT name=evt_latency n=%lu min_us=%lu avg_us=%lu max_us=%lu\r\n", (unsigned long)ev.count, (unsigned long)cyc2us(ev.min), (unsigned long)cyc2us(perf_avg(&ev)), (unsigned long)cyc2us(ev.max));
    perf_stat_t m; taskENTER_CRITICAL(); m = st_cmd; taskEXIT_CRITICAL();
    printf("STAT name=cmd_latency n=%lu min_us=%lu avg_us=%lu max_us=%lu\r\n", (unsigned long)m.count, (unsigned long)cyc2us(m.min),
           (unsigned long)cyc2us(perf_avg(&m)), (unsigned long)cyc2us(m.max));
    printf("STAT name=cam_frame n=%lu min_us=%lu avg_us=%lu max_us=%lu\r\n", (unsigned long)f.count,
           (unsigned long)cyc2us(f.min), (unsigned long)cyc2us(perf_avg(&f)), (unsigned long)cyc2us(f.max));
    uint32_t now_cyc = platform_cycles();
    taskENTER_CRITICAL(); rts_flush(&g_rts, now_cyc); uint64_t total = rts_total_cyc(&g_rts); uint64_t idl = rts_run_cyc(&g_rts, (uint32_t)xTaskGetIdleTaskHandle()); taskEXIT_CRITICAL();
    uint32_t run_pct = total ? (uint32_t)((total - idl) * 100U / total) : 0;
    printf("LOAD pct=%lu run_pct=%lu btn_lost=%lu uart_rx_overflow=%lu pend_lost=%lu flash_err=%lu imu_err=%lu cam_err=%lu flash_last_rc=%ld\r\n", (unsigned long)last_load_pct,
           (unsigned long)run_pct, (unsigned long)btn_lost, (unsigned long)(platform_uart_rx_overflow() + rx_lost), (unsigned long)pend_lost,
           (unsigned long)flash_err, (unsigned long)imu_err, (unsigned long)cam_err, (long)flash_last_rc);
}

void product_reset_stats(void)
{
    taskENTER_CRITICAL();
    for (int i = 0; i < nrt; i++) rt_reset_one(&rt[i]);
    perf_stat_init(&st_btn, "btn_latency"); perf_stat_init(&st_evt, "evt_latency"); perf_stat_init(&st_cam_frame, "cam_frame"); perf_stat_init(&st_cmd, "cmd_latency");
    btn_presses = 0; btn_lost = 0; rx_lost = 0; pend_lost = 0;
    rts_reset(&g_rts, platform_cycles()); win_busy_base = 0; win_cyc0 = platform_cycles(); stats_t0_us = platform_micros();
    taskEXIT_CRITICAL();
    rtos_tick_reset(); rtos_tick_gap_reset(); rtos_switch_gap_reset(); platform_cam_diag_reset();
    printf("[prod] stats reset\r\n");
}

void product_print_mode(void)
{
    printf("[prod] mode=%s light=%lu thr=%lu ex=%ld ey=%ld az=%u el=%u servos=%s\r\n", tracker_mode_name(trk.mode),
           (unsigned long)trk.light, (unsigned long)trk.cfg.dark_light_thr, (long)trk.ex, (long)trk.ey, trk.az_us, trk.el_us, servos_on ? "ON" : "OFF");
}

void product_print_sky(void)
{
    printf("[prod] sky=%s frame=%s mean=%u contrast=%u sat_permille=%u next_frame=%lu\r\n", sky_name(sky), have_frame ? "yes" : "no",
           fst.mean, fst.contrast, fst.sat_permille, (unsigned long)frame_next);
}

void product_print_mem(void)
{
    printf("MEM stack_used=%lu stack_reserved=%lu heap_arena=%lu frame_buf=%u\r\n", (unsigned long)platform_stack_used(),
           (unsigned long)platform_stack_reserved(), (unsigned long)platform_heap_used(), (unsigned)sizeof g_frame);
}

void product_print_heap(void)
{
    printf("HEAP total=%u free=%u min_ever_free=%u used_max=%u malloc_failed=%lu\r\n", (unsigned)configTOTAL_HEAP_SIZE, (unsigned)xPortGetFreeHeapSize(),
           (unsigned)xPortGetMinimumEverFreeHeapSize(), (unsigned)(configTOTAL_HEAP_SIZE - xPortGetMinimumEverFreeHeapSize()), (unsigned long)rtos_malloc_failed_count());
}

static const char *state_name(eTaskState s)
{
    switch (s) { case eRunning: return "Running"; case eReady: return "Ready"; case eBlocked: return "Blocked"; case eSuspended: return "Suspended"; default: return "Deleted"; }
}

void product_print_tasks(void)
{
    static TaskStatus_t st[16];
    UBaseType_t n = uxTaskGetSystemState(st, 16, NULL);
    uint32_t now_cyc = platform_cycles();
    taskENTER_CRITICAL(); rts_flush(&g_rts, now_cyc); taskEXIT_CRITICAL();
    uint64_t total = rts_total_cyc(&g_rts); uint32_t hz_us = platform_cpu_hz() / 1000000U;
    for (UBaseType_t i = 0; i < n; i++) {
        uint64_t run = rts_run_cyc(&g_rts, (uint32_t)st[i].xHandle);
        uint32_t pct10 = total ? (uint32_t)(run * 1000U / total) : 0;
        printf("TASK name=%s state=%s prio=%u cpu_us=%lu cpu_pct=%lu.%lu stack_free_words=%u\r\n", st[i].pcTaskName, state_name(st[i].eCurrentState),
               (unsigned)st[i].uxCurrentPriority, (unsigned long)(run / hz_us), (unsigned long)(pct10 / 10), (unsigned long)(pct10 % 10), (unsigned)st[i].usStackHighWaterMark);
    }
    printf("TASKS total_us=%lu switches=%lu\r\n", (unsigned long)(total / hz_us), (unsigned long)g_rts.total_switches);
}

void product_print_stack(void)
{
    static TaskStatus_t st[16];
    UBaseType_t n = uxTaskGetSystemState(st, 16, NULL);
    for (UBaseType_t i = 0; i < n; i++) {
        unsigned size = 0;
        for (int k = 0; k < nrt; k++) if (rt[k].h == st[i].xHandle) size = rt[k].stack_words;
        if (!size) size = (strcmp(st[i].pcTaskName, "IDLE") == 0) ? configMINIMAL_STACK_SIZE : 0;
        printf("STACK task=%s size_words=%u free_min_words=%u used_max_bytes=%u\r\n", st[i].pcTaskName, size, (unsigned)st[i].usStackHighWaterMark,
               size ? (size - (unsigned)st[i].usStackHighWaterMark) * 4U : 0U);
    }
    printf("MEM stack_used=%lu stack_reserved=%lu (MSP: обробники переривань)\r\n", (unsigned long)platform_stack_used(), (unsigned long)platform_stack_reserved());
}

void product_print_oscost(void)
{
    uint32_t n, avg, max; rtos_tick_stats(&n, &avg, &max);
    uint32_t hz = platform_cpu_hz();
    printf("OSCOST tick_hz=%u tick_isr_n=%lu tick_isr_avg_cyc=%lu tick_isr_max_cyc=%lu tick_isr_avg_us_x100=%lu tick_isr_max_us_x100=%lu\r\n",
           (unsigned)configTICK_RATE_HZ, (unsigned long)n, (unsigned long)avg, (unsigned long)max,
           (unsigned long)((uint64_t)avg * 100000000U / hz), (unsigned long)((uint64_t)max * 100000000U / hz));
    /* вікно — за 64-бітним лічильником тактів обліку задач (platform_micros переповнювався, якщо його не читали > 43 с) */
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

void product_trace_cmd(int argc, char **argv)
{
    if (argc >= 1 && !strcmp(argv[0], "ARM")) { taskENTER_CRITICAL(); rts_trace_arm(&g_rts); taskEXIT_CRITICAL(); printf("[trace] armed (%u switches)\r\n", (unsigned)RTS_TRACE_N); return; }
    uint32_t cnt = g_rts.ev_count, from = 0, lim = 0;
    if (argc >= 2) { for (const char *s = argv[1]; *s >= '0' && *s <= '9'; s++) lim = lim * 10 + (uint32_t)(*s - '0'); }
    if (lim && lim < cnt) cnt = lim;
    printf("[trace] %lu events, t in us from first\r\n", (unsigned long)cnt);
    uint32_t t0 = cnt ? g_rts.ev[0].cyc : 0, hz_us = platform_cpu_hz() / 1000000U;
    for (uint32_t i = from; i < cnt; i++) {
        const char *nm = pcTaskGetName((TaskHandle_t)g_rts.ev[i].id);
        printf("TRACE t_us=%lu task=%s\r\n", (unsigned long)((g_rts.ev[i].cyc - t0) / hz_us), nm ? nm : "?");
    }
}

void product_btnsim(uint32_t period_ms) { platform_btnsim(period_ms); }
void product_cam_irq_mode(int mode) { platform_cam_set_irq_mode(mode); }

void product_cal_show(void)
{
    const tracker_cfg_t *c = &trk.cfg;
    printf("[cal] AZ %u..%u us, EL %u..%u us, park %u/%u, dark_thr=%lu, invert=%d, sign az=%d el=%d, zero ex=%d ey=%d\r\n", c->az_min_us, c->az_max_us,
           c->el_min_us, c->el_max_us, c->park_az_us, c->park_el_us, (unsigned long)c->dark_light_thr, c->invert_light, c->sign_az, c->sign_el, c->ex_off, c->ey_off);
}

static int parse_u(const char *s, uint32_t *out)
{
    if (!s || !*s) return -1;
    uint32_t v = 0;
    for (; *s; s++) { if (*s < '0' || *s > '9') return -1; v = v * 10 + (uint32_t)(*s - '0'); }
    *out = v; return 0;
}

static int cal_cmd_locked(int argc, char **argv)
{
    tracker_cfg_t *c = &trk.cfg;
    if (argc < 1) return -1;
    if (!strcmp(argv[0], "AZ") || !strcmp(argv[0], "EL")) {
        uint32_t lo, hi;
        if (argc < 3 || parse_u(argv[1], &lo) || parse_u(argv[2], &hi) || calib_check_range((uint16_t)lo, (uint16_t)hi)) return -1;
        if (argv[0][0] == 'A') { c->az_min_us = (uint16_t)lo; c->az_max_us = (uint16_t)hi; }
        else { c->el_min_us = (uint16_t)lo; c->el_max_us = (uint16_t)hi; }
        trk.az_us = trk.az_us < c->az_min_us ? c->az_min_us : trk.az_us > c->az_max_us ? c->az_max_us : trk.az_us;
        trk.el_us = trk.el_us < c->el_min_us ? c->el_min_us : trk.el_us > c->el_max_us ? c->el_max_us : trk.el_us;
        return 0;
    }
    if (!strcmp(argv[0], "ZERO")) { c->ex_off = (int16_t)(trk.ex + c->ex_off); c->ey_off = (int16_t)(trk.ey + c->ey_off); return 0; }
    if (!strcmp(argv[0], "DEADBAND")) { uint32_t v; if (argc < 2 || parse_u(argv[1], &v) || v > 500) return -1; c->deadband_permille = (uint16_t)v; return 0; }
    if (!strcmp(argv[0], "DARK")) { uint32_t v; if (argc < 2 || parse_u(argv[1], &v)) return -1; c->dark_light_thr = v; return 0; }
    if (!strcmp(argv[0], "INV"))  { uint32_t v; if (argc < 2 || parse_u(argv[1], &v) || v > 1) return -1; c->invert_light = (int8_t)v; return 0; }
    if (!strcmp(argv[0], "SIGN")) {
        if (argc < 3 || (strcmp(argv[2], "+1") && strcmp(argv[2], "-1"))) return -1;
        int8_t v = argv[2][0] == '-' ? -1 : 1;
        if (!strcmp(argv[1], "AZ")) c->sign_az = v; else if (!strcmp(argv[1], "EL")) c->sign_el = v; else return -1;
        return 0;
    }
    if (!strcmp(argv[0], "SAVE"))    { if (!flash_ok) return -1; xSemaphoreTake(flash_mx, portMAX_DELAY); int rc = calib_save(&flash, c); xSemaphoreGive(flash_mx); if (rc == DEV_OK) log_event(EV_CAL, 0, 0, 0); return rc; }
    if (!strcmp(argv[0], "LOAD"))    { if (!flash_ok) return -1; xSemaphoreTake(flash_mx, portMAX_DELAY); int rc = calib_load(&flash, c); xSemaphoreGive(flash_mx); return rc; }
    if (!strcmp(argv[0], "DEFAULT")) { tracker_default_cfg(c); return 0; }
    return -1;
}

int product_cal_cmd(int argc, char **argv) { return cal_cmd_locked(argc, argv); }

void product_log_dump(int n)
{
    if (!flash_ok) { printf("[log] flash unavailable\r\n"); return; }
    xSemaphoreTake(flash_mx, portMAX_DELAY);
    int32_t idx = evlog_newest_idx(&elog);
    uint32_t valid = elog.valid;
    ev_rec_t recs[16]; int cnt = 0;
    for (int i = 0; i < n && i < 16 && idx >= 0; i++, idx = (int32_t)((idx + EVLOG_CAPACITY - 1) % EVLOG_CAPACITY)) {
        if (evlog_read(&elog, (uint32_t)idx, &recs[cnt]) != DEV_OK) break;
        cnt++;
    }
    xSemaphoreGive(flash_mx);
    printf("[log] %lu records in flash (capacity %u)\r\n", (unsigned long)valid, (unsigned)EVLOG_CAPACITY);
    for (int i = 0; i < cnt; i++) {
        ev_rec_t *r = &recs[i];
        printf("[log] #%u t=%lu ms type=%u frame=%u a=%d b=%d c=%d\r\n", r->seq, (unsigned long)r->t_ms, r->type, r->frame, r->a, r->b, r->c);
    }
}

int product_log_erase(void)
{
    if (!flash_ok) return -1;
    xSemaphoreTake(flash_mx, portMAX_DELAY);
    int rc = 0;
    for (uint32_t i = 0; i < EVLOG_SECTORS; i++) if (w25q_sector_erase(&flash, EVLOG_BASE_ADDR + i * W25Q_SECTOR_SIZE) != DEV_OK) { rc = -1; break; }
    if (rc == 0) rc = evlog_open(&elog, &flash);
    xSemaphoreGive(flash_mx);
    return rc;
}

void product_frames_info(void)
{
    printf("[frames] next=%lu, ring of %u slots, frame %u x %u px, %u bytes\r\n", (unsigned long)frame_next, (unsigned)FRAMESTORE_SLOTS,
           (unsigned)FRAME_W, (unsigned)FRAME_H, (unsigned)FRAME_PIXELS);
}

int product_frame_show(uint32_t no)
{
    if (!flash_ok) return -1;
    xSemaphoreTake(frame_mx, portMAX_DELAY); xSemaphoreTake(flash_mx, portMAX_DELAY);
    int rc = framestore_load(&flash, no, g_frame, FRAME_PIXELS);
    xSemaphoreGive(flash_mx);
    frame_stats_t s = frame_stats(g_frame, FRAME_PIXELS, SKY_DEFAULTS.sat_level);
    xSemaphoreGive(frame_mx);
    if (rc != DEV_OK) return -1;
    printf("[frames] #%lu mean=%u contrast=%u sat_permille=%u sky=%s\r\n", (unsigned long)no, s.mean, s.contrast, s.sat_permille,
           sky_name(sky_classify(&s, &SKY_DEFAULTS)));
    return 0;
}

void product_snapshot_request(void) { snap_request(); }

void product_servo_power(int on)
{
    if (on) {
        trk.az_us = trk.cfg.park_az_us; trk.el_us = trk.cfg.park_el_us;
        platform_servo_set_us(PLATFORM_SERVO_AZ, trk.az_us); platform_servo_set_us(PLATFORM_SERVO_EL, trk.el_us);
        platform_servo_enable(PLATFORM_SERVO_AZ, 1); platform_servo_enable(PLATFORM_SERVO_EL, 1);
    } else {
        platform_servo_enable(PLATFORM_SERVO_AZ, 0); platform_servo_enable(PLATFORM_SERVO_EL, 0);
    }
    servos_on = on;
}

int product_cam_exposure(uint32_t aec, uint32_t gain)
{
    if (aec > 0xFFFF || gain > 0xFF) return -1;
    ov7670_t cam; ov7670_init_dev(&cam, platform_sccb_cam());
    return ov7670_set_exposure(&cam, (uint16_t)aec, (uint8_t)gain);
}

#ifndef GIT_HASH
#define GIT_HASH "dev"
#endif
void product_print_version(void)
{
    printf("VER git=%s build=\"%s %s\" sysclk=%lu task_tick_us=1 os=FreeRTOS-%s tick_hz=%u\r\n", GIT_HASH, __DATE__, __TIME__, (unsigned long)platform_cpu_hz(),
           tskKERNEL_VERSION_NUMBER, (unsigned)configTICK_RATE_HZ);
}

int product_set_period(const char *name, uint32_t ms)
{
    if (ms < 1 || ms > 600000) return -1;
    for (int i = 0; i < nrt; i++) if (!strcmp(rt[i].name, name) && rt[i].period_ms) { rt[i].period_ms = ms; rt[i].rebase = 1; xTaskAbortDelay(rt[i].h); return 0; }
    return -1;
}

void product_cmd_started(void)
{
    taskENTER_CRITICAL(); perf_add(&st_cmd, platform_cycles() - platform_uart_eol_cycles()); taskEXIT_CRITICAL();   /* Enter у ISR -> початок виконання команди */
}

uint8_t *product_frame_buf(void) { return g_frame; }

void product_probe_test(void)
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

void product_sky_force(int v) { sky_force = v; if (v >= 0) sky = (sky_t)v; }

int product_test(int argc, char **argv) { return rtos_test_run(argc, argv); }
