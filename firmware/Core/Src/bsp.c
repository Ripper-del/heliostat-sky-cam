/**
 * @file bsp.c — ЛР5: склейка QP/C, FreeRTOS і плати. Тут (і лише тут, разом з main.c, rtos_port.c, meas.c) видно FreeRTOS:
 * статична пам'ять AO (черги, стеки, пули подій), пріоритети, старт, хук тіку, переривання -> події, Q_onError.
 * Логіка продукту — в AO (App/Src/ao_*.c), вони користуються лише API QP.
 */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ao.h"
#include "meas.h"
#include "product.h"
#include "platform.h"
#include "rtos_port.h"
#include "mpu6050.h"
#include "ov7670.h"
#include "evlog.h"

Q_DEFINE_THIS_MODULE("bsp")

/* ---- пули подій: у порядку зростання розміру блоку (п. 5.10 методички) ---- */
typedef union { BtnEvt a; QuadEvt b; TargetEvt c; SkyEvt d; LogEvt e; FrameEvt f; } SmallEvt;
typedef union { TrackEvt a; CmdEvt b; LineEvt c; CalEvt d; } MidEvt;
#ifndef BSP_POOL1_N
#define BSP_POOL1_N 64U
#endif
static QF_MPOOL_EL(SmallEvt) pool1[BSP_POOL1_N];
static QF_MPOOL_EL(MidEvt)   pool2[24];
static QF_MPOOL_EL(TextEvt)  pool3[32];
static QSubscrList subscrSto[MAX_PUB_SIG];

/* ---- AO: ім'я (як задача в ЛР4), QP-пріоритет, пріоритет задачі FreeRTOS (як у ЛР4), черга, стек ----
 * Черги підписників TRACK (tick, loss, led, snapshot) довші за пул pool2 (24 блоки): публікація не може їх переповнити,
 * навіть якщо AO довго не отримує процесор (довгий друк cli). Періодичні події займають у черзі не більше одного місця. */
#ifndef BSP_STACK_SCALE_PCT
#define BSP_STACK_SCALE_PCT 100                          /* сценарій «переповнення стека»: -DBSP_STACK_SCALE_PCT=... */
#endif
#define STK(w) (((w) * BSP_STACK_SCALE_PCT) / 100U)
#define AO_DEF(id_, nm_, qp_, os_, qlen_, stkw_) \
    static QEvtPtr q_##id_[qlen_]; static StackType_t s_##id_[STK(stkw_)];
#ifdef LOG_OWNER_HIGH                                    /* варіант: власник flash вище за всіх клієнтів (4), п. 5.8 */
#define CLI_QP 7
#define LOG_QP 8
#define LOG_OS 4
#else                                                    /* основний: log на пріоритеті задачі ЛР4 (2), див. qp-design.md */
#define CLI_QP 8
#define LOG_QP 7
#define LOG_OS 2
#endif
AO_DEF(cli,      "cli",      CLI_QP, 3, 32, 384)
AO_DEF(log,      "log",      LOG_QP, LOG_OS, 16, 320)
AO_DEF(button,   "button",  12, 6,  8, 128)
AO_DEF(adc,      "adc",     11, 5,  4, 128)
AO_DEF(control,  "control", 10, 4, 16, 320)
AO_DEF(servo,    "servo",    9, 4,  8, 128)
AO_DEF(imu,      "imu",      6, 2,  8, 256)
#ifndef STK_TICK_W
#define STK_TICK_W 320                                   /* сценарій «переповнення стека»: -DSTK_TICK_W=96 */
#endif
AO_DEF(tick,     "tick",     5, 2, 32, STK_TICK_W)
AO_DEF(loss,     "loss",     4, 2, 32, 192)
AO_DEF(led,      "led",      3, 1, 32, 128)
AO_DEF(camera,   "camera",   2, 1,  8, 320)
AO_DEF(snapshot, "snapshot", 1, 1, 32, 192)

static BootInfo boot;

static void start(int id, QActive *ao, char const *name, uint8_t qp, uint8_t os, QEvtPtr *q, uint16_t qlen, StackType_t *stk, uint16_t stkw)
{
    meas_rt_t *r = &g_rt[id];
    r->name = name; r->ao = ao; r->qp_prio = qp; r->os_prio = os; r->queue_len = qlen; r->stack_words = stkw;
    QActive_setAttr(ao, TASK_NAME_ATTR, name);
    QActive_start(ao, QF_prioSpec(qp, os), q, qlen, stk, (uint_fast16_t)(stkw * sizeof(StackType_t)), &boot);
}
#define START(id_, AO_, nm_, qp_, os_) start(id_, AO_, #nm_, qp_, os_, q_##nm_, (uint16_t)Q_DIM(q_##nm_), s_##nm_, (uint16_t)Q_DIM(s_##nm_))

/* ================= старт ================= */
void product_init(void)
{
    /* 1. Усе, де драйвер може чекати HAL_Delay, — ДО першого виклику ядра (після нього до старту планувальника IRQ вимкнені). */
    Log_boot(&boot);
    if (boot.flash_ok) {
        if (boot.cal_defaults) printf("[prod] calibration: defaults (flash empty)\r\n");
        printf("[prod] log: %lu records, next frame #%lu, open %lu ms\r\n", (unsigned long)boot.log_records, (unsigned long)boot.frame_next, (unsigned long)boot.open_ms);
    }
    { mpu6050_t imu; mpu6050_init_dev(&imu, platform_i2c1()); mpu6050_wake(&imu); }
    { ov7670_t cam; ov7670_init_dev(&cam, platform_sccb_cam()); if (ov7670_config_qqvga_yuv(&cam) != DEV_OK) g_cam_err++; }

    /* 2. QP: пули, таблиця підписників, AO. Порядок старту: cli першим (інші вже в початковому переході можуть писати йому),
     *    log — до тих, хто шле йому запити. */
    QF_init();
    QF_poolInit(pool1, sizeof pool1, sizeof pool1[0]);
    QF_poolInit(pool2, sizeof pool2, sizeof pool2[0]);
    QF_poolInit(pool3, sizeof pool3, sizeof pool3[0]);
    QActive_psInit(subscrSto, Q_DIM(subscrSto));
    meas_init();
    Cli_ctor(); Log_ctor(); Button_ctor(); Adc_ctor(); Control_ctor(); Servo_ctor();
    Imu_ctor(); Tick_ctor(); Loss_ctor(); Led_ctor(); Camera_ctor(); Snapshot_ctor();
    START(AO_CLI,      AO_Cli,      cli,      CLI_QP, 3);
    START(AO_LOG,      AO_Log,      log,      LOG_QP, LOG_OS);
    START(AO_BUTTON,   AO_Button,   button,  12, 6);
    START(AO_ADC,      AO_Adc,      adc,     11, 5);
    START(AO_CONTROL,  AO_Control,  control, 10, 4);
    START(AO_SERVO,    AO_Servo,    servo,    9, 4);
    START(AO_IMU,      AO_Imu,      imu,      6, 2);
    START(AO_TICK,     AO_Tick,     tick,     5, 2);
    START(AO_LOSS,     AO_Loss,     loss,     4, 2);
    START(AO_LED,      AO_Led,      led,      3, 1);
    START(AO_CAMERA,   AO_Camera,   camera,   2, 1);
    START(AO_SNAPSHOT, AO_Snapshot, snapshot, 1, 1);
    ao_log(AO_Cli, EV_BOOT, EVLOG_NO_FRAME, 0, 0, 0);
    printf("[prod] %u AO (QP/C %s on FreeRTOS), tick=%u Hz, heap=%u B, boot_ms=%lu\r\n", (unsigned)AO_COUNT, QP_VERSION_STR,
           (unsigned)configTICK_RATE_HZ, (unsigned)configTOTAL_HEAP_SIZE, (unsigned long)platform_millis());
    meas_print_version();
}

void product_start(void)
{
    (void)QF_run();                                         /* QF_onStartup() і vTaskStartScheduler(); не повертається */
    for (;;) { }
}

/* ================= переривання -> події (лише FromISR-API) ================= */
static void btn_isr(uint32_t isr_cyc, uint32_t evt_cyc)
{
    BaseType_t woken = pdFALSE;
    BtnEvt *b;
    Q_NEW_X_FROM_ISR(b, BtnEvt, 2U, BUTTON_SIG);
    if (!b) g_btn_lost++;
    else {
        b->isr_cyc = isr_cyc; b->evt_cyc = evt_cyc;
        if (!QACTIVE_POST_X_FROM_ISR(AO_Button, &b->super, 1U, &woken, 0)) g_btn_lost++;   /* черга повна: подію втрачено з лічильником */
    }
    portYIELD_FROM_ISR(woken);
}

/* UART RX: рядок збирається в перериванні (приватний буфер ISR), у cli йде вже готовий рядок — подія LINE.
 * Правило розбору те саме, що cli_feed(): рядок починається з великої літери, інакше це односимвольна команда. */
static char     rx_line[47];
static uint8_t  rx_len;
static uint32_t rx_last_ms;
static void post_line(char const *s, uint8_t n)
{
    BaseType_t woken = pdFALSE;
    LineEvt *l;
    Q_NEW_X_FROM_ISR(l, LineEvt, 2U, LINE_SIG);
    if (!l) { g_rx_lost++; return; }
    memcpy(l->s, s, n); l->n = n; l->eol_cyc = platform_uart_eol_cycles();
#ifdef LR5_RX_NO_MARGIN
    QACTIVE_POST_FROM_ISR(AO_Cli, &l->super, &woken, 0);    /* п. 9.3.4: без запасу — повна черга cli = Q_onError qf_port */
#else
    if (!QACTIVE_POST_X_FROM_ISR(AO_Cli, &l->super, 1U, &woken, 0)) g_rx_lost++;   /* повна черга: рядок втрачено з лічильником */
#endif
    portYIELD_FROM_ISR(woken);
}
static void rx_isr(uint8_t b)
{
    uint32_t now = platform_millis();
    if (rx_len && now - rx_last_ms > 2000U) rx_len = 0;       /* як у ЛР4: недописаний рядок старший за 2 с скидається */
    rx_last_ms = now;
    if (b == '\r' || b == '\n') { if (rx_len) { post_line(rx_line, rx_len); rx_len = 0; } return; }
    if (rx_len == 0 && !(b >= 'A' && b <= 'Z')) { char c = (char)b; post_line(&c, 1); return; }
    if (rx_len < sizeof rx_line) rx_line[rx_len++] = (char)b;
}

static void catchup(uint32_t ms) { xTaskCatchUpTicks(ms); }

/* ================= колбеки QP ================= */
void QF_onStartup(void)
{
    platform_set_button_hook(btn_isr);
    platform_set_uart_hook(rx_isr);
    platform_cam_set_irq_mode(1);                           /* як у ЛР4: IRQ вимкнені лише на активну частину рядка */
    platform_cam_set_catchup_hook(catchup);
#ifdef LR5_FLASH_MUTEX
    { extern void meas_delay_hook(uint32_t ms); platform_set_delay_hook(meas_delay_hook); }   /* п. 9.3.1: драйвер чекає з блокуванням, як у ЛР4 */
#endif
}

void QF_onCleanup(void) { }

/* Контракт QP порушено (черга або пул вичерпано, повторний armX, ...): причину в UART і перезапуск */
Q_NORETURN Q_onError(char const * const module, int_t const id)
{
    static char msg[48];
    snprintf(msg, sizeof msg, "Q_onError %s %d", module, (int)id);
    rtos_fatal(msg);
    for (;;) { }
}

/* хук тіку FreeRTOS (контекст переривання SysTick): обслуговує часові події QP */
void vApplicationTickHook(void)
{
    BaseType_t woken = pdFALSE;
    QTIMEEVT_TICK_FROM_ISR(0U, &woken, 0);
    portYIELD_FROM_ISR(woken);
}

/* статична пам'ять задачі простою (configSUPPORT_STATIC_ALLOCATION = 1, вимога порту QP) */
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stk, uint32_t *n)
{
    static StaticTask_t idle_tcb; static StackType_t idle_stk[configMINIMAL_STACK_SIZE];
    *tcb = &idle_tcb; *stk = idle_stk; *n = configMINIMAL_STACK_SIZE;
}

