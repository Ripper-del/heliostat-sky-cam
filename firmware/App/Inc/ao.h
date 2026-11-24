/**
 * @file ao.h — ЛР5: активні об'єкти (AO) продукту на QP/C поверх FreeRTOS. Сигнали, події, пріоритети.
 * Кожна задача ЛР4 стала AO з тим самим ім'ям (STAT name=... у логах не змінились). AO взаємодіють лише подіями;
 * спільних змінних і м'ютексів у файлах AO немає. Схема — docs/qp-design.md, діаграми — docs/statecharts/.
 */
#ifndef AO_H
#define AO_H
#include <stdint.h>
#include "qpc.h"
#include "tracker.h"
#include "sky.h"

/* ---- сигнали ---- */
enum Signals {
    /* публікуються (публікація-підписка), мають бути меншими за MAX_PUB_SIG */
    TRACK_SIG = Q_USER_SIG,  /* control -> led, loss, tick, snapshot: стан трекера (10 Гц) */
    SKY_SIG,                 /* camera -> control, tick: клас неба за кадром */
    MAX_PUB_SIG,
    /* часові події (кожен AO має свою) */
    PERIOD_SIG = MAX_PUB_SIG, /* період AO (adc 10 мс, control 10 мс, servo 20 мс, ...) */
    POLL_SIG,                 /* log: перевірити, чи flash закінчила стирання або запис */
    OP_TIMEOUT_SIG,           /* snapshot: операція не завершилась вчасно */
    /* з переривань */
    BUTTON_SIG,               /* EXTI кнопки B1 (або імітатор BTNSIM): BtnEvt */
    LINE_SIG,                 /* UART RX: рядок або односимвольна команда: LineEvt */
    /* пряме надсилання */
    SAMPLE_SIG,               /* adc -> control: відлік квадранта (QuadEvt) */
    TARGET_SIG,               /* control -> servo: цілі серво (TargetEvt) */
    SERVO_PWR_SIG,            /* control -> servo: увімкнути/вимкнути (TargetEvt, on) */
    TEXT_SIG,                 /* будь-який AO -> cli: рядок для UART (TextEvt) */
    CMD_SIG,                  /* cli -> власник даних: команда користувача (CmdEvt) */
    SNAP_REQ_SIG,             /* button/control/loss/cli -> snapshot: зробити знімок */
    CAPTURE_REQ_SIG,          /* snapshot -> camera: захопити кадр у буфер */
    CAPTURE_DONE_SIG,         /* camera -> snapshot: кадр у буфері (FrameEvt), буфер позичено */
    SAVE_FRAME_SIG,           /* snapshot -> log: записати кадр (FrameEvt) */
    SAVE_DONE_SIG,            /* log -> snapshot: кадр записано (FrameEvt, rc) */
    BUF_RELEASE_SIG,          /* snapshot -> camera: буфер кадру знову вільний */
    FRAME_LOAD_SIG,           /* camera -> log: прочитати кадр n у буфер (FrameEvt) */
    FRAME_LOADED_SIG,         /* log -> camera: кадр прочитано (FrameEvt, rc) */
    LOG_REQ_SIG,              /* будь-хто -> log: запис журналу (LogEvt) */
    CAL_SAVE_SIG,             /* control -> log: зберегти калібровку (CalEvt) */
    CAL_LOAD_SIG,             /* control -> log: прочитати калібровку */
    CAL_DONE_SIG,             /* log -> control: результат (CalEvt) */
    MAX_SIG
};

/* ---- ідентифікатори і пріоритети AO ----
 * QP-пріоритет унікальний (1..12); пріоритет задачі FreeRTOS — той самий, що в ЛР4 (QF_prioSpec), щоб порівняння
 * з ЛР4 показувало ефект моделі AO, а не інший розподіл пріоритетів. Виняток: log, власник flash, піднятий з 2 до 4 —
 * вище за своїх клієнтів, що чекають відповіді (cli, snapshot, camera), п. 5.8 методички. */
enum AoId { AO_BUTTON, AO_ADC, AO_CONTROL, AO_SERVO, AO_LOG, AO_CLI, AO_IMU, AO_TICK, AO_LOSS, AO_LED, AO_CAMERA, AO_SNAPSHOT, AO_COUNT };

/* ---- події з параметрами ---- */
typedef struct { QEvt super; uint32_t isr_cyc, evt_cyc; } BtnEvt;                    /* моменти DWT: вхід у ISR і сама подія */
typedef struct { QEvt super; uint8_t n; char s[47]; uint32_t eol_cyc; } LineEvt;      /* рядок без \r\n; n = 1 і s[0] малий — односимвольна команда */
typedef struct { QEvt super; uint16_t q[4]; } QuadEvt;
typedef struct { QEvt super; uint16_t az, el; uint8_t on; } TargetEvt;
typedef struct {
    QEvt super;
    uint16_t q[4]; uint32_t light, dark_thr; int32_t ex, ey; uint8_t mode;
} TrackEvt;
typedef struct { QEvt super; uint8_t sky, mean; } SkyEvt;
typedef struct { QEvt super; char s[136]; } TextEvt;
typedef struct { QEvt super; uint8_t code; int32_t a, b; char text[40]; } CmdEvt;
typedef struct {
    QEvt super;
    uint8_t *buf; uint32_t no; int16_t rc; uint8_t mean, mode; uint32_t light; uint32_t t_ms;
} FrameEvt;
typedef struct { QEvt super; uint8_t type, frame; int16_t a, b, c; uint32_t t_ms, post_cyc; } LogEvt;
typedef struct { QEvt super; tracker_cfg_t cfg; int16_t rc; uint8_t load; } CalEvt;

/* коди команд CMD_SIG (хто виконує — у коментарі) */
enum CmdCode {
    CMD_MODE = 1,       /* control: [prod] mode=... */
    CMD_CAL,            /* control: text = аргументи CAL */
    CMD_SERVO,          /* control: a = 1/0 */
    CMD_SKY,            /* camera: [prod] sky=... */
    CMD_SKY_FORCE,      /* camera: a = -1 (AUTO) / 0 / 1 / 2 */
    CMD_CAM_EXP,        /* camera: a = aec, b = gain */
    CMD_FRAME_SHOW,     /* camera: a = номер кадру */
    CMD_FRAMES_INFO,    /* log */
    CMD_LOG_DUMP,       /* log: a = кількість */
    CMD_LOG_ERASE,      /* log */
    CMD_PERIOD,         /* будь-який періодичний AO: a = мс */
    CMD_FAIL_STEP,      /* сценарії QP/C п. 9.3: a = номер сценарію, b = параметр */
};

/* Що прочитано з flash до старту планувальника (bsp.c); передається AO параметром початкового переходу. */
typedef struct { tracker_cfg_t cfg; uint8_t flash_ok, cal_defaults; uint32_t frame_next, log_records, open_ms; } BootInfo;
void Log_boot(BootInfo *b);      /* log: прочитати flash ДО QF_init (див. bsp.c) */


/* ---- AO (визначені у своїх файлах App/Src/ao_*.c) ---- */
extern QActive * const AO_Button;
extern QActive * const AO_Adc;
extern QActive * const AO_Control;
extern QActive * const AO_Servo;
extern QActive * const AO_Log;
extern QActive * const AO_Cli;
extern QActive * const AO_Imu;
extern QActive * const AO_Tick;
extern QActive * const AO_Loss;
extern QActive * const AO_Led;
extern QActive * const AO_Camera;
extern QActive * const AO_Snapshot;

void Button_ctor(void); void Adc_ctor(void); void Control_ctor(void); void Servo_ctor(void);
void Log_ctor(void); void Cli_ctor(void); void Imu_ctor(void); void Tick_ctor(void);
void Loss_ctor(void); void Led_ctor(void); void Camera_ctor(void); void Snapshot_ctor(void);
uint8_t *Camera_service_buf(void);   /* лише службова самоперевірка ЛР2 (див. qp-design.md, винятки) */

/* ---- допоміжне для AO (App/Src/ao_util.c) ---- */
#define BSP_TICKS_PER_SEC 1000U                /* = configTICK_RATE_HZ */
#define MS(ms_) ((uint32_t)((((uint32_t)(ms_)) * BSP_TICKS_PER_SEC + 999U) / 1000U))
void ao_say(QActive const *sender, char const *fmt, ...);                         /* TEXT_SIG до cli: друкує лише cli (власник UART) */
void ao_log(QActive const *sender, uint8_t type, uint8_t frame, int16_t a, int16_t b, int16_t c);   /* LOG_REQ_SIG до log */
void ao_snap(QActive const *sender);                                               /* SNAP_REQ_SIG до snapshot */
/* періодична робота: одноразова часова подія, яку AO перезапускає на наступний ідеальний тік (як vTaskDelayUntil у ЛР4) */
void ao_period_arm(QTimeEvt *te, int id, uint32_t ms);                             /* запустити (або змінити період, PERIOD task ms) */
int  ao_period(QTimeEvt *te, int id, uint32_t t0);                                 /* на PERIOD_SIG: облік джитера, перезапуск; 1 — робити роботу */
#ifdef LR5_FLASH_MUTEX
int  Log_direct_save(FrameEvt const *f, uint32_t *no);   /* сценарій 3.1: кадр пише snapshot сам під м'ютексом */
void Log_direct_flush(void);
#endif
#endif
