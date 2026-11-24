/* ЛР5: AO snapshot (задача snapshot у ЛР4). Керує операцією «знімок у flash» (FR-11): кадр знімає camera, пише log.
 * Ієрархічна машина: idle і busy { capturing, saving }.
 *   idle      — чекає SNAP_REQ; не частіше ніж раз на 3 с (умова на переході);
 *   busy      — суперстан операції: entry запускає таймаут і піднімає пін D3, exit знімає пін, зупиняє таймаут і записує
 *               тривалість операції; нові SNAP_REQ під час операції ігноруються (описано один раз тут, а не в кожному підстані);
 *   capturing — запит CAPTURE_REQ до camera, чекаємо CAPTURE_DONE;
 *   saving    — запит SAVE_FRAME до log, чекаємо SAVE_DONE, повертаємо буфер camera.
 * Запізнілі CAPTURE_DONE / SAVE_DONE (після таймауту) приходять в idle: там буфер просто повертається camera. */
#include "ao.h"
#include "meas.h"
#include "platform.h"

typedef struct {
    QActive  super;
    QTimeEvt timeout;
    uint8_t  flash_ok, mode, mean;
    uint32_t light, last_ms, t_op0;
    uint8_t *buf;
} Snapshot;
static Snapshot l_snap;
QActive * const AO_Snapshot = &l_snap.super;

static QState Snapshot_initial(Snapshot *me, void const *par);
static QState Snapshot_idle(Snapshot *me, QEvt const *e);
static QState Snapshot_busy(Snapshot *me, QEvt const *e);
static QState Snapshot_capturing(Snapshot *me, QEvt const *e);
static QState Snapshot_saving(Snapshot *me, QEvt const *e);

void Snapshot_ctor(void)
{
    QActive_ctor(&l_snap.super, Q_STATE_CAST(&Snapshot_initial));
    QTimeEvt_ctorX(&l_snap.timeout, &l_snap.super, OP_TIMEOUT_SIG, 0U);
}

static QState Snapshot_initial(Snapshot *me, void const *par)
{
    me->flash_ok = ((BootInfo const *)par)->flash_ok;
    me->last_ms = 0U - 3000U;
    QActive_subscribe(&me->super, TRACK_SIG);
    return Q_TRAN(&Snapshot_idle);
}

static void release(Snapshot *me, int16_t rc, uint32_t no)
{
    Q_UNUSED_PAR(me);                                       /* відправник потрібен лише трасуванню QS */
    FrameEvt *f = Q_NEW_X(FrameEvt, 1U, BUF_RELEASE_SIG);
    if (!f) { g_ev_lost++; return; }
    f->rc = rc; f->no = no;
    if (!QACTIVE_POST_X(AO_Camera, &f->super, 0U, &me->super)) g_ev_lost++;
}

static QState Snapshot_idle(Snapshot *me, QEvt const *e)
{
    switch (e->sig) {
    case TRACK_SIG: me->light = Q_EVT_CAST(TrackEvt)->light; me->mode = Q_EVT_CAST(TrackEvt)->mode; return Q_HANDLED();
    case SNAP_REQ_SIG: {
        uint32_t now = platform_millis();
        if (me->flash_ok && now - me->last_ms >= 3000U) { me->last_ms = now; return Q_TRAN(&Snapshot_capturing); }
        return Q_HANDLED();                                 /* FR-11: не частіше ніж раз на 3 с */
    }
    case CAPTURE_DONE_SIG:                                  /* запізніле після таймауту: кадр не потрібен, буфер повертаємо */
        g_test.late_events++;
        if (Q_EVT_CAST(FrameEvt)->rc == 0) release(me, -1, 0);
        return Q_HANDLED();
    case SAVE_DONE_SIG:                                     /* запізніле після таймауту: log закінчив писати, буфер вільний */
        g_test.late_events++;
        release(me, Q_EVT_CAST(FrameEvt)->rc, Q_EVT_CAST(FrameEvt)->no);
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState Snapshot_busy(Snapshot *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG:
        platform_probe(3, 1);                               /* D3 аналізатора: уся операція знімка */
        me->t_op0 = platform_cycles();
        QTimeEvt_armX(&me->timeout, MS(g_test.snap_timeout_ms ? g_test.snap_timeout_ms : 10000U), 0U);
        return Q_HANDLED();
    case Q_EXIT_SIG:
        (void)QTimeEvt_disarm(&me->timeout);
        platform_probe(3, 0);
        meas_step(AO_SNAPSHOT, me->t_op0);                  /* STAT snapshot: тривалість усієї операції, як у ЛР4 */
        return Q_HANDLED();
    case TRACK_SIG: me->light = Q_EVT_CAST(TrackEvt)->light; me->mode = Q_EVT_CAST(TrackEvt)->mode; return Q_HANDLED();
    case SNAP_REQ_SIG:                                      /* знімок уже йде */
        return Q_HANDLED();
    case OP_TIMEOUT_SIG:
        g_flash_err++; g_flash_last_rc = -10;
        return Q_TRAN(&Snapshot_idle);
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState Snapshot_capturing(Snapshot *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG: {
        static QEvt const capEvt = QEVT_INITIALIZER(CAPTURE_REQ_SIG);
        if (!QACTIVE_POST_X(AO_Camera, &capEvt, 1U, &me->super)) g_ev_lost++;
        return Q_HANDLED();
    }
    case CAPTURE_DONE_SIG: {
        FrameEvt const *f = Q_EVT_CAST(FrameEvt);
        if (f->rc != 0) return Q_TRAN(&Snapshot_idle);      /* кадр не знято (cam_err рахує camera) */
        me->buf = f->buf; me->mean = f->mean;
        return Q_TRAN(&Snapshot_saving);
    }
    default:
        return Q_SUPER(&Snapshot_busy);
    }
}

static QState Snapshot_saving(Snapshot *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG: {
#ifdef LR5_FLASH_MUTEX
        {   /* сценарій 3.1: snapshot сам пише кадр під м'ютексом (як задача snapshot у ЛР4); відповідь — собі ж */
            FrameEvt tmp; tmp.buf = me->buf; tmp.light = me->light; tmp.mode = me->mode; tmp.mean = me->mean; tmp.t_ms = platform_millis();
            uint32_t no = 0; int rc = Log_direct_save(&tmp, &no);
            FrameEvt *d = Q_NEW_X(FrameEvt, 1U, SAVE_DONE_SIG);
            if (d) { d->rc = (int16_t)rc; d->no = no; QACTIVE_POST(&me->super, &d->super, &me->super); }
            return Q_HANDLED();
        }
#endif
        FrameEvt *f = Q_NEW_X(FrameEvt, 1U, SAVE_FRAME_SIG);
        if (!f) { g_ev_lost++; release(me, -1, 0); return Q_HANDLED(); }
        f->buf = me->buf; f->light = me->light; f->mode = me->mode; f->mean = me->mean; f->t_ms = platform_millis(); f->rc = 0;
        if (!QACTIVE_POST_X(AO_Log, &f->super, 1U, &me->super)) { g_ev_lost++; release(me, -1, 0); }
        return Q_HANDLED();
    }
    case SAVE_DONE_SIG: {
        FrameEvt const *f = Q_EVT_CAST(FrameEvt);
        release(me, f->rc, f->no);
        return Q_TRAN(&Snapshot_idle);
    }
    default:
        return Q_SUPER(&Snapshot_busy);
    }
}
