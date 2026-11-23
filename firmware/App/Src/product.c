/**
 * product.c — продукт ЛР3: часовий super loop. Таблиця задач (період, порядок = пріоритет), усі вимірювання
 * (час виконання, запізнення, пропуски, завантаження CPU) ведуться планувальником sched.c.
 * Довгі блокувальні операції: захоплення кадру (~0,4…0,8 с), запис кадру у flash, стирання сектора журналу.
 */
#include <stdio.h>
#include <string.h>
#include "product.h"
#include "cli.h"
#include "platform.h"
#include "sched.h"
#include "perf.h"
#include "evlog.h"
#include "framestore.h"
#include "calib.h"
#include "mpu6050.h"
#include "ov7670.h"
#include "w25q.h"
#include "quadrant.h"
#include "sky.h"

/* ---- стан ---- */
static sched_t  sched;
static w25q_t   flash;
static int      flash_ok;
static evlog_t  elog;
static tracker_t trk;
static loss_t   loss;
static uint32_t frame_next = 1;
static uint8_t  g_frame[FRAME_PIXELS];
static int      have_frame;
static frame_stats_t fst;
static sky_t    sky = SKY_OVERCAST;
static int      sky_force = -1;                /* діагностика: -1 = за камерою, інакше задана класифікація */
static uint16_t q[4];
static int      servos_on;
static int      snap_req;
static uint32_t last_snap_ms, last_suspect_ms;
static uint32_t btn_presses;
static uint32_t flash_err, imu_err, cam_err;
static mpu6050_t imu;
static mpu6050_raw_t imu_raw;
static uint32_t seconds;

/* ---- черга подій для журналу (щоб не писати у flash прямо з швидких задач) ---- */
#define PEND_N 8
typedef struct { uint8_t type, frame; uint32_t t_ms; int16_t a, b, c; } pend_t;
static pend_t pend[PEND_N]; static uint8_t pend_head, pend_tail; static uint32_t pend_lost;

static void log_event(uint8_t type, int16_t a, int16_t b, int16_t c)
{
    uint8_t next = (uint8_t)((pend_head + 1) % PEND_N);
    if (next == pend_tail) { pend_lost++; return; }
    pend[pend_head] = (pend_t){ type, EVLOG_NO_FRAME, platform_millis(), a, b, c };
    pend_head = next;
}

/* ---- додаткова статистика (поза задачами) ---- */
static perf_stat_t st_loop, st_btn, st_evt, st_cam_frame, st_cmd;
static perf_hist_t hist_loop;
static uint32_t loop_prev_cyc;
static uint32_t window_start_cyc; static uint64_t window_busy_base;
static uint32_t last_load_pct;
static uint32_t stats_t0_us;                 /* початок вікна статистики (мкс), для середнього завантаження CPU за весь прогін */

/* ================= задачі ================= */
static void adc_task(void)
{
    platform_probe(0, 1);
    platform_adc_read_quadrant(q);
    platform_probe(0, 0);
}

static void control_task(void)
{
    uint32_t now = platform_millis();
    track_mode_t before = trk.mode;
    int changed = tracker_update(&trk, q, sky, now);
    if (changed) {
        log_event(EV_MODE, (int16_t)before, (int16_t)trk.mode, (int16_t)(trk.light / 4));
        if (trk.mode == MODE_PARK) snap_req = 1;                       /* FR-14: вхід у PARK = знімок */
    }
}

static void servo_task(void)
{
    if (!servos_on) return;
    uint32_t a = servo_next_step(platform_servo_get_us(PLATFORM_SERVO_AZ), trk.az_us, 20);
    uint32_t e = servo_next_step(platform_servo_get_us(PLATFORM_SERVO_EL), trk.el_us, 20);
    platform_servo_set_us(PLATFORM_SERVO_AZ, a);
    platform_servo_set_us(PLATFORM_SERVO_EL, e);
}

static void led_task(void)
{
    static uint8_t n;
    n++;
    /* TRACK: 5 Гц; OVERCAST: 1 Гц; PARK: короткий спалах раз на 2 с. Крок задачі 100 мс. */
    if (trk.mode == MODE_TRACK) platform_led_toggle();
    else if (trk.mode == MODE_OVERCAST) { if (n % 5 == 0) platform_led_toggle(); }
    else platform_marker((n % 20) == 0);
}

static void imu_task(void)
{
    if (mpu6050_read_accel_raw(&imu, &imu_raw) != DEV_OK) imu_err++;   /* FR-17: кут нахилу з прискорення */
}

static void camera_task(void)
{
    platform_probe(2, 1);
    uint32_t c0 = platform_cycles();
    int rc = platform_cam_capture(g_frame, FRAME_PIXELS, 0, 1500);
    perf_add(&st_cam_frame, platform_cycles() - c0);
    platform_probe(2, 0);
    if (rc != 0) { cam_err++; return; }
    have_frame = 1;
    fst = frame_stats(g_frame, FRAME_PIXELS, SKY_DEFAULTS.sat_level);
    sky = sky_force >= 0 ? (sky_t)sky_force : sky_classify(&fst, &SKY_DEFAULTS);   /* FR-12 (SKY FORCE — лише для перевірок) */
    int ldr_dark = trk.light < trk.cfg.dark_light_thr;
    uint32_t now = platform_millis();
    if (sky_mismatch(sky, ldr_dark) && now - last_suspect_ms >= 10000U) {   /* FR-16 */
        last_suspect_ms = now;
        log_event(EV_SENSOR_SUSPECT, fst.mean, (int16_t)(trk.light / 4), (int16_t)sky);
    }
}

static void snapshot_task(void)
{
    uint32_t now = platform_millis();
    if (!snap_req || !flash_ok || now - last_snap_ms < 3000U) return;   /* FR-11: не частіше ніж раз на 3 с */
    snap_req = 0; last_snap_ms = now;
    platform_probe(3, 1);
    if (platform_cam_capture(g_frame, FRAME_PIXELS, 0, 1500) != 0) { cam_err++; platform_probe(3, 0); return; }
    have_frame = 1;
    uint32_t no = frame_next;
    int rc = framestore_save(&flash, no, g_frame, FRAME_PIXELS);     /* стирання 5 секторів + запис 19 КБ */
    if (rc == DEV_OK) {
        frame_next++;
        rc = evlog_append(&elog, EV_SIGNAL_LOSS, (uint8_t)(no & 0xFF), now, (int16_t)(trk.light / 4), (int16_t)trk.mode, fst.mean);
    }
    if (rc != DEV_OK) flash_err++;
    platform_probe(3, 0);
}

static void log_task(void)
{
    if (!flash_ok || pend_tail == pend_head) return;
    pend_t *p = &pend[pend_tail];
    if (evlog_append(&elog, p->type, p->frame, p->t_ms, p->a, p->b, p->c) != DEV_OK) flash_err++;
    pend_tail = (uint8_t)((pend_tail + 1) % PEND_N);
}

static void loss_task(void)                                           /* FR-14: падіння суми на 50 % за 2 с */
{
    if (loss_update(&loss, trk.light, platform_millis())) { log_event(EV_SIGNAL_LOSS, (int16_t)(trk.light / 4), 0, 0); snap_req = 1; }
}

static void button_task(void)
{
    uint32_t t;
    if (!platform_button_event(&t)) return;
    platform_probe(4, 1);                                              /* початок реакції (видно на аналізаторі) */
    perf_add(&st_btn, platform_cycles() - t);                          /* ISR -> початок реакції, такти */
    perf_add(&st_evt, platform_cycles() - platform_button_event_cycles());   /* подія -> початок реакції (ЛР4) */
    btn_presses++;
    snap_req = 1;
    platform_probe(4, 0);
}

static void cli_task(void)
{
    static uint32_t last_rx_ms;
    int c, got = 0;
    while ((c = platform_uart_getc()) >= 0) { cli_feed((char)c); got = 1; }
    if (got) last_rx_ms = platform_millis();
    else if (platform_millis() - last_rx_ms > 2000U) cli_reset();       /* недописаний рядок (байти втрачено) не живе вічно */
}

static void tick_task(void)
{
    seconds++;
    uint32_t now_cyc = platform_cycles();
    uint32_t window = now_cyc - window_start_cyc;
    uint64_t busy = sched.busy_cyc - window_busy_base;
    last_load_pct = window ? (uint32_t)(busy * 100U / window) : 0;
    window_start_cyc = now_cyc; window_busy_base = sched.busy_cyc;
    quadrant_error_t e = quadrant_error(q);
    printf("tick %lu (uptime %lu s, TL=%u TR=%u BL=%u BR=%u, ex=%ld ey=%ld) mode=%s sky=%s load=%lu%%\r\n",
           (unsigned long)platform_millis(), (unsigned long)seconds, q[0], q[1], q[2], q[3],
           (long)e.x_permille, (long)e.y_permille, tracker_mode_name(trk.mode), sky_name(sky), (unsigned long)last_load_pct);
}

/* ================= таблиця задач ================= */
static task_t tasks[] = {
    TASK_DEF("button",   button_task,   1000),
    TASK_DEF("cli",      cli_task,      1000),
    TASK_DEF("adc",      adc_task,      10000),
    TASK_DEF("control",  control_task,  10000),
    TASK_DEF("servo",    servo_task,    20000),
    TASK_DEF("loss",     loss_task,     100000),
    TASK_DEF("imu",      imu_task,      100000),
    TASK_DEF("led",      led_task,      100000),
    TASK_DEF("log",      log_task,      5000),
    TASK_DEF("snapshot", snapshot_task, 100000),
    TASK_DEF("camera",   camera_task,   3000000),
    TASK_DEF("tick",     tick_task,     1000000),
};
#define NTASKS (sizeof tasks / sizeof tasks[0])

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
    perf_stat_init(&st_loop, "loop"); perf_stat_init(&st_btn, "btn_latency"); perf_stat_init(&st_evt, "evt_latency"); perf_stat_init(&st_cam_frame, "cam_frame"); perf_stat_init(&st_cmd, "cmd_latency");
    perf_hist_init(&hist_loop, 100);
    sched_init(&sched, tasks, NTASKS, platform_micros, platform_cycles);
    stats_t0_us = platform_micros();
    window_start_cyc = platform_cycles(); window_busy_base = 0; loop_prev_cyc = window_start_cyc;
    log_event(EV_BOOT, 0, 0, 0);
    printf("[prod] %u tasks, boot_ms=%lu\r\n", (unsigned)NTASKS, (unsigned long)platform_millis());
    product_print_version();
}

void product_step(void)
{
    uint32_t now = platform_cycles();
    uint32_t hz_us = platform_cpu_hz() / 1000000U;
    perf_add(&st_loop, (now - loop_prev_cyc) / hz_us);               /* період головного циклу, мкс */
    perf_hist_add(&hist_loop, (now - loop_prev_cyc) / hz_us);
    loop_prev_cyc = now;
    sched_run_once(&sched);
}

/* ================= діагностика ================= */
static uint32_t cyc2us(uint32_t c) { return c / (platform_cpu_hz() / 1000000U); }

void product_print_stats(void)
{
    for (size_t i = 0; i < NTASKS; i++) {
        task_t *t = &tasks[i];
        printf("STAT name=%s n=%lu min_us=%lu avg_us=%lu max_us=%lu late_max_us=%lu late_p99_us=%lu missed=%lu max_cyc=%lu\r\n",
               t->name, (unsigned long)t->runs, (unsigned long)cyc2us(t->exec_cyc.min), (unsigned long)cyc2us(perf_avg(&t->exec_cyc)),
               (unsigned long)cyc2us(t->exec_cyc.max), (unsigned long)t->late_us.max,
               (unsigned long)perf_hist_percentile(&t->late_hist, 99), (unsigned long)t->missed, (unsigned long)t->exec_cyc.max);
    }
    printf("STAT name=loop n=%lu min_us=%lu avg_us=%lu max_us=%lu p99_us=%lu\r\n", (unsigned long)st_loop.count,
           (unsigned long)st_loop.min, (unsigned long)perf_avg(&st_loop), (unsigned long)st_loop.max,
           (unsigned long)perf_hist_percentile(&hist_loop, 99));
    printf("STAT name=btn_latency n=%lu min_us=%lu avg_us=%lu max_us=%lu presses=%lu\r\n", (unsigned long)st_btn.count,
           (unsigned long)cyc2us(st_btn.min), (unsigned long)cyc2us(perf_avg(&st_btn)), (unsigned long)cyc2us(st_btn.max), (unsigned long)btn_presses);
    printf("STAT name=evt_latency n=%lu min_us=%lu avg_us=%lu max_us=%lu\r\n", (unsigned long)st_evt.count, (unsigned long)cyc2us(st_evt.min), (unsigned long)cyc2us(perf_avg(&st_evt)), (unsigned long)cyc2us(st_evt.max));
    printf("STAT name=cmd_latency n=%lu min_us=%lu avg_us=%lu max_us=%lu\r\n", (unsigned long)st_cmd.count, (unsigned long)cyc2us(st_cmd.min),
           (unsigned long)cyc2us(perf_avg(&st_cmd)), (unsigned long)cyc2us(st_cmd.max));
    printf("STAT name=cam_frame n=%lu min_us=%lu avg_us=%lu max_us=%lu\r\n", (unsigned long)st_cam_frame.count,
           (unsigned long)cyc2us(st_cam_frame.min), (unsigned long)cyc2us(perf_avg(&st_cam_frame)), (unsigned long)cyc2us(st_cam_frame.max));
    uint32_t el_us = platform_micros() - stats_t0_us;
    uint32_t run_pct = el_us ? (uint32_t)(sched.busy_cyc / (platform_cpu_hz() / 1000000U) * 100U / el_us) : 0;
    printf("LOAD pct=%lu run_pct=%lu btn_lost=%lu uart_rx_overflow=%lu pend_lost=%lu flash_err=%lu imu_err=%lu cam_err=%lu\r\n", (unsigned long)last_load_pct, (unsigned long)run_pct, (unsigned long)platform_button_overruns(),
           (unsigned long)platform_uart_rx_overflow(), (unsigned long)pend_lost, (unsigned long)flash_err,
           (unsigned long)imu_err, (unsigned long)cam_err);
}

void product_reset_stats(void)
{
    sched_reset_stats(&sched);
    perf_stat_init(&st_loop, "loop"); perf_stat_init(&st_btn, "btn_latency"); perf_stat_init(&st_evt, "evt_latency"); perf_stat_init(&st_cam_frame, "cam_frame"); perf_stat_init(&st_cmd, "cmd_latency");
    perf_hist_init(&hist_loop, 100);
    window_start_cyc = platform_cycles(); window_busy_base = 0; stats_t0_us = platform_micros();
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

int product_cal_cmd(int argc, char **argv)
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
    if (!strcmp(argv[0], "ZERO")) { c->ex_off = (int16_t)(trk.ex + c->ex_off); c->ey_off = (int16_t)(trk.ey + c->ey_off); return 0; }   /* поточний перекіс = нуль */
    if (!strcmp(argv[0], "DEADBAND")) { uint32_t v; if (argc < 2 || parse_u(argv[1], &v) || v > 500) return -1; c->deadband_permille = (uint16_t)v; return 0; }
    if (!strcmp(argv[0], "DARK")) { uint32_t v; if (argc < 2 || parse_u(argv[1], &v)) return -1; c->dark_light_thr = v; return 0; }
    if (!strcmp(argv[0], "INV"))  { uint32_t v; if (argc < 2 || parse_u(argv[1], &v) || v > 1) return -1; c->invert_light = (int8_t)v; return 0; }
    if (!strcmp(argv[0], "SIGN")) {
        if (argc < 3 || (strcmp(argv[2], "+1") && strcmp(argv[2], "-1"))) return -1;
        int8_t v = argv[2][0] == '-' ? -1 : 1;
        if (!strcmp(argv[1], "AZ")) c->sign_az = v; else if (!strcmp(argv[1], "EL")) c->sign_el = v; else return -1;
        return 0;
    }
    if (!strcmp(argv[0], "SAVE"))    { if (!flash_ok) return -1; int rc = calib_save(&flash, c); if (rc == DEV_OK) log_event(EV_CAL, 0, 0, 0); return rc; }
    if (!strcmp(argv[0], "LOAD"))    { return flash_ok ? calib_load(&flash, c) : -1; }
    if (!strcmp(argv[0], "DEFAULT")) { tracker_default_cfg(c); return 0; }
    return -1;
}

void product_log_dump(int n)
{
    if (!flash_ok) { printf("[log] flash unavailable\r\n"); return; }
    int32_t idx = evlog_newest_idx(&elog);
    printf("[log] %lu records in flash (capacity %u)\r\n", (unsigned long)elog.valid, (unsigned)EVLOG_CAPACITY);
    for (int i = 0; i < n && idx >= 0; i++, idx = (int32_t)((idx + EVLOG_CAPACITY - 1) % EVLOG_CAPACITY)) {
        ev_rec_t r;
        if (evlog_read(&elog, (uint32_t)idx, &r) != DEV_OK) break;
        printf("[log] #%u t=%lu ms type=%u frame=%u a=%d b=%d c=%d\r\n", r.seq, (unsigned long)r.t_ms, r.type, r.frame, r.a, r.b, r.c);
    }
}

int product_log_erase(void)
{
    if (!flash_ok) return -1;
    for (uint32_t i = 0; i < EVLOG_SECTORS; i++) if (w25q_sector_erase(&flash, EVLOG_BASE_ADDR + i * W25Q_SECTOR_SIZE) != DEV_OK) return -1;
    return evlog_open(&elog, &flash);
}

void product_frames_info(void)
{
    printf("[frames] next=%lu, ring of %u slots, frame %u x %u px, %u bytes\r\n", (unsigned long)frame_next, (unsigned)FRAMESTORE_SLOTS,
           (unsigned)FRAME_W, (unsigned)FRAME_H, (unsigned)FRAME_PIXELS);
}

int product_frame_show(uint32_t no)
{
    if (!flash_ok || framestore_load(&flash, no, g_frame, FRAME_PIXELS) != DEV_OK) return -1;
    frame_stats_t s = frame_stats(g_frame, FRAME_PIXELS, SKY_DEFAULTS.sat_level);
    printf("[frames] #%lu mean=%u contrast=%u sat_permille=%u sky=%s\r\n", (unsigned long)no, s.mean, s.contrast, s.sat_permille,
           sky_name(sky_classify(&s, &SKY_DEFAULTS)));
    return 0;
}

void product_snapshot_request(void) { snap_req = 1; }

void product_servo_power(int on)
{
    if (on) {
        trk.az_us = trk.cfg.park_az_us; trk.el_us = trk.cfg.park_el_us;      /* старт з безпечного положення, без ривка до межі */
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
    printf("VER git=%s build=\"%s %s\" sysclk=%lu task_tick_us=1\r\n", GIT_HASH, __DATE__, __TIME__, (unsigned long)platform_cpu_hz());
}

int product_set_period(const char *name, uint32_t ms)
{
    if (ms < 1 || ms > 600000) return -1;
    for (size_t i = 0; i < NTASKS; i++) {
        if (!strcmp(tasks[i].name, name)) { tasks[i].period_us = ms * 1000U; tasks[i].due_us = platform_micros() + ms * 1000U; return 0; }
    }
    return -1;
}

void product_cmd_started(void)
{
    perf_add(&st_cmd, platform_cycles() - platform_uart_eol_cycles());      /* Enter у ISR -> початок виконання команди */
}

uint8_t *product_frame_buf(void) { return g_frame; }

void product_probe_test(void)
{
    uint32_t hz_us = platform_cpu_hz() / 1000000U;
    uint32_t t0 = platform_cycles(), next[PLATFORM_PROBE_COUNT];
    int lvl[PLATFORM_PROBE_COUNT] = {0};
    for (int k = 0; k < PLATFORM_PROBE_COUNT; k++) next[k] = 0;
    while (platform_cycles() - t0 < 4000000U * hz_us) {                 /* 4 с */
        uint32_t el = (platform_cycles() - t0) / hz_us;                 /* мкс від старту */
        for (int k = 0; k < PLATFORM_PROBE_COUNT; k++) {
            if (el >= next[k]) { lvl[k] ^= 1; platform_probe(k, lvl[k]); next[k] = el + (100U << k); }   /* півперіод 100 мкс · 2^k */
        }
    }
    for (int k = 0; k < PLATFORM_PROBE_COUNT; k++) platform_probe(k, 0);
}

void product_sky_force(int v) { sky_force = v; if (v >= 0) sky = (sky_t)v; }

void product_btnsim(uint32_t period_ms) { platform_btnsim(period_ms); }
