/* ЛР5: AO camera (задача camera у ЛР4). Власник камери OV7670 і буфера кадру 19 200 Б (у ЛР4 — м'ютекс frame_mx).
 * Ієрархічна машина: ready { free, lent }.
 *   free — буфер вільний: кожні 3 с кадр для класифікації неба; на запит snapshot кадр знімається і буфер ПОЗИЧАЄТЬСЯ;
 *   lent — буфером користується інший AO (log пише кадр у flash або читає кадр з flash); камера не пише в буфер,
 *          періодичний кадр пропускається (missed), а буфер повертає подія BUF_RELEASE або FRAME_LOADED.
 * Захоплення кадру — один довгий крок (0,4–1,2 с): камера віддає рядки безперервно, розбити кадр на кроки між іншими
 * подіями не можна. Тому camera має найнижчий пріоритет; витісняти її можуть усі інші AO (у вікнах між рядками). */
#include <string.h>
#include "ao.h"
#include "meas.h"
#include "platform.h"
#include "ov7670.h"

typedef struct {
    QActive  super;
    QTimeEvt period;
    uint8_t  buf[FRAME_PIXELS];
    frame_stats_t fst;
    sky_t    sky;
    int8_t   force;          /* -1: класифікація за кадром; 0..2: SKY FORCE */
    uint8_t  have_frame;
    uint32_t frame_next, show_no;
} Camera;
static Camera l_camera;
QActive * const AO_Camera = &l_camera.super;

static QState Camera_initial(Camera *me, void const *par);
static QState Camera_ready(Camera *me, QEvt const *e);
static QState Camera_free(Camera *me, QEvt const *e);
static QState Camera_lent(Camera *me, QEvt const *e);

/* Буфер для службової самоперевірки ЛР2 (односимвольна команда g і самоперевірка на старті, до запуску AO). */
uint8_t *Camera_service_buf(void) { return l_camera.buf; }

void Camera_ctor(void)
{
    QActive_ctor(&l_camera.super, Q_STATE_CAST(&Camera_initial));
    QTimeEvt_ctorX(&l_camera.period, &l_camera.super, PERIOD_SIG, 0U);
}

static QState Camera_initial(Camera *me, void const *par)
{
    me->frame_next = ((BootInfo const *)par)->frame_next;
    me->sky = SKY_OVERCAST; me->force = -1;
    ao_period_arm(&me->period, AO_CAMERA, 3000U);
    return Q_TRAN(&Camera_free);
}

static void publish_sky(Camera *me)
{
    SkyEvt *s = Q_NEW_X(SkyEvt, 4U, SKY_SIG);
    if (!s) { g_ev_lost++; return; }
    s->sky = (uint8_t)me->sky; s->mean = me->fst.mean;
    QACTIVE_PUBLISH(&s->super, &me->super);
}

static int capture(Camera *me, int pin)
{
    platform_probe(pin, 1);
    uint32_t c0 = platform_cycles();
    int rc = platform_cam_capture(me->buf, FRAME_PIXELS, 0, 1500);
    uint32_t cyc = platform_cycles() - c0;
    platform_probe(pin, 0);
    if (rc != 0) { g_cam_err++; return rc; }
    meas_add(M_CAM_FRAME, cyc);
    me->have_frame = 1;
    me->fst = frame_stats(me->buf, FRAME_PIXELS, SKY_DEFAULTS.sat_level);
    return 0;
}

/* суперстан: команди, що діють однаково, вільний буфер чи ні */
static QState Camera_ready(Camera *me, QEvt const *e)
{
    switch (e->sig) {
    case CMD_SIG: {
        CmdEvt const *c = Q_EVT_CAST(CmdEvt);
        switch (c->code) {
        case CMD_SKY:
            ao_say(&me->super, "[prod] sky=%s frame=%s mean=%u contrast=%u sat_permille=%u next_frame=%lu", sky_name(me->sky), me->have_frame ? "yes" : "no",
                   me->fst.mean, me->fst.contrast, me->fst.sat_permille, (unsigned long)me->frame_next);
            break;
        case CMD_SKY_FORCE:
            me->force = (int8_t)c->a;
            if (c->a >= 0) { me->sky = (sky_t)c->a; publish_sky(me); }
            break;
        case CMD_CAM_EXP: {                     /* SCCB з паузами драйвера ~15 мс: службова команда, див. виняток у qp-design.md */
            ov7670_t cam; ov7670_init_dev(&cam, platform_sccb_cam());
            int ok = c->a <= 0xFFFF && c->b <= 0xFF && ov7670_set_exposure(&cam, (uint16_t)c->a, (uint8_t)c->b) == 0;
            ao_say(&me->super, "[cam] %s", ok ? "exposure set" : "ERROR (CAM EXP aec gain)");
            break;
        }
        case CMD_PERIOD: ao_period_arm(&me->period, AO_CAMERA, (uint32_t)c->a); break;
        case CMD_FRAME_SHOW: ao_say(&me->super, "[frames] busy"); break;   /* у free перехоплюється */
        default: break;
        }
        return Q_HANDLED();
    }
    case CAPTURE_REQ_SIG: {                     /* буфер зайнятий: одразу відповідаємо помилкою, ніхто не чекає вічно */
        FrameEvt *f = Q_NEW_X(FrameEvt, 2U, CAPTURE_DONE_SIG);
        if (f) { f->rc = -2; f->buf = 0; if (!QACTIVE_POST_X(AO_Snapshot, &f->super, 1U, &me->super)) g_ev_lost++; } else g_ev_lost++;
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState Camera_free(Camera *me, QEvt const *e)
{
    switch (e->sig) {
    case PERIOD_SIG: {
        uint32_t t0 = platform_cycles();
        if (ao_period(&me->period, AO_CAMERA, t0) && capture(me, 2) == 0) {
            me->sky = me->force >= 0 ? (sky_t)me->force : sky_classify(&me->fst, &SKY_DEFAULTS);
            publish_sky(me);
        }
        meas_step(AO_CAMERA, t0);
        return Q_HANDLED();
    }
    case CAPTURE_REQ_SIG: {                     /* знімок: кадр у буфер, буфер позичаємо snapshot -> log */
        uint32_t t0 = platform_cycles();
        int rc = capture(me, 5);                /* 5: службовий пін не задіяний (кадр знімка видно на D3 від snapshot) */
        meas_step(AO_CAMERA, t0);
        FrameEvt *f = Q_NEW_X(FrameEvt, 2U, CAPTURE_DONE_SIG);
        if (!f) { g_ev_lost++; return Q_HANDLED(); }
        f->rc = (int16_t)rc; f->buf = rc == 0 ? me->buf : 0; f->mean = me->fst.mean;
        if (!QACTIVE_POST_X(AO_Snapshot, &f->super, 1U, &me->super)) { g_ev_lost++; return Q_HANDLED(); }
        return rc == 0 ? Q_TRAN(&Camera_lent) : Q_HANDLED();
    }
    case CMD_SIG:
        if (Q_EVT_CAST(CmdEvt)->code == CMD_FRAME_SHOW) {   /* FRAMES n: log читає кадр у наш буфер */
            FrameEvt *f = Q_NEW_X(FrameEvt, 2U, FRAME_LOAD_SIG);
            if (!f) { g_ev_lost++; return Q_HANDLED(); }
            me->show_no = (uint32_t)Q_EVT_CAST(CmdEvt)->a;
            f->buf = me->buf; f->no = me->show_no; f->rc = 0;
            if (!QACTIVE_POST_X(AO_Log, &f->super, 1U, &me->super)) { g_ev_lost++; return Q_HANDLED(); }
            return Q_TRAN(&Camera_lent);
        }
        return Q_SUPER(&Camera_ready);
    default:
        return Q_SUPER(&Camera_ready);
    }
}

static QState Camera_lent(Camera *me, QEvt const *e)
{
    switch (e->sig) {
    case PERIOD_SIG: {                          /* буфер позичено: кадр неба пропускаємо (у ЛР4 задача чекала на м'ютексі) */
        uint32_t t0 = platform_cycles();
        if (ao_period(&me->period, AO_CAMERA, t0)) g_rt[AO_CAMERA].missed++;
        return Q_HANDLED();
    }
    case BUF_RELEASE_SIG: {                     /* snapshot: кадр записано (або не вдалося), буфер знову наш */
        FrameEvt const *f = Q_EVT_CAST(FrameEvt);
        if (f->rc == 0) me->frame_next = f->no + 1U;
        return Q_TRAN(&Camera_free);
    }
    case FRAME_LOADED_SIG: {                    /* log прочитав кадр n для FRAMES n */
        FrameEvt const *f = Q_EVT_CAST(FrameEvt);
        if (f->rc == 0) {
            frame_stats_t s = frame_stats(me->buf, FRAME_PIXELS, SKY_DEFAULTS.sat_level);
            ao_say(&me->super, "[frames] #%lu mean=%u contrast=%u sat_permille=%u sky=%s", (unsigned long)me->show_no, s.mean, s.contrast, s.sat_permille,
                   sky_name(sky_classify(&s, &SKY_DEFAULTS)));
        } else ao_say(&me->super, "[frames] no such frame");
        me->have_frame = 0;                     /* у буфері тепер старий кадр із flash, а не останній з камери */
        return Q_TRAN(&Camera_free);
    }
    default:
        return Q_SUPER(&Camera_ready);
    }
}
