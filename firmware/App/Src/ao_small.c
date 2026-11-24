/* ЛР5: три невеликі періодичні AO з ЛР4 — loss (100 мс), led (100 мс), tick (1 с). Кожен — окремий AO у своїй задачі;
 * в одному файлі лише тому, що кожен займає кілька рядків. Стан трекера отримують підпискою на TRACK (10 Гц). */
#include <string.h>
#include "ao.h"
#include "meas.h"
#include "platform.h"
#include "evlog.h"
#include "quadrant.h"

/* ================= loss: FR-14, падіння освітленості більш ніж на 50 % за 2 с ================= */
typedef struct { QActive super; QTimeEvt period; loss_t loss; uint32_t light; } Loss;
static Loss l_loss;
QActive * const AO_Loss = &l_loss.super;
static QState Loss_initial(Loss *me, void const *par);
static QState Loss_watching(Loss *me, QEvt const *e);

void Loss_ctor(void)
{
    QActive_ctor(&l_loss.super, Q_STATE_CAST(&Loss_initial));
    QTimeEvt_ctorX(&l_loss.period, &l_loss.super, PERIOD_SIG, 0U);
}
static QState Loss_initial(Loss *me, void const *par)
{
    Q_UNUSED_PAR(par);
    loss_init(&me->loss);
    QActive_subscribe(&me->super, TRACK_SIG);
    ao_period_arm(&me->period, AO_LOSS, 100U);
    return Q_TRAN(&Loss_watching);
}
static QState Loss_watching(Loss *me, QEvt const *e)
{
    switch (e->sig) {
    case TRACK_SIG: me->light = Q_EVT_CAST(TrackEvt)->light; return Q_HANDLED();
    case PERIOD_SIG: {
        uint32_t t0 = platform_cycles();
        if (ao_period(&me->period, AO_LOSS, t0) && loss_update(&me->loss, me->light, platform_millis())) {
            ao_log(&me->super, EV_SIGNAL_LOSS, EVLOG_NO_FRAME, (int16_t)(me->light / 4), 0, 0);
            ao_snap(&me->super);
        }
        meas_step(AO_LOSS, t0);
        return Q_HANDLED();
    }
    case CMD_SIG: if (Q_EVT_CAST(CmdEvt)->code == CMD_PERIOD) ao_period_arm(&me->period, AO_LOSS, (uint32_t)Q_EVT_CAST(CmdEvt)->a); return Q_HANDLED();
    default: return Q_SUPER(&QHsm_top);
    }
}

/* ================= led: індикація режиму ================= */
typedef struct { QActive super; QTimeEvt period; uint8_t mode, n; } Led;
static Led l_led;
QActive * const AO_Led = &l_led.super;
static QState Led_initial(Led *me, void const *par);
static QState Led_blinking(Led *me, QEvt const *e);

void Led_ctor(void)
{
    QActive_ctor(&l_led.super, Q_STATE_CAST(&Led_initial));
    QTimeEvt_ctorX(&l_led.period, &l_led.super, PERIOD_SIG, 0U);
}
static QState Led_initial(Led *me, void const *par)
{
    Q_UNUSED_PAR(par);
    me->mode = MODE_OVERCAST;
    QActive_subscribe(&me->super, TRACK_SIG);
    ao_period_arm(&me->period, AO_LED, 100U);
    return Q_TRAN(&Led_blinking);
}
static QState Led_blinking(Led *me, QEvt const *e)
{
    switch (e->sig) {
    case TRACK_SIG: me->mode = Q_EVT_CAST(TrackEvt)->mode; return Q_HANDLED();
    case PERIOD_SIG: {
        uint32_t t0 = platform_cycles();
        if (ao_period(&me->period, AO_LED, t0)) {
            me->n++;
            if (me->mode == MODE_TRACK) platform_led_toggle();
            else if (me->mode == MODE_OVERCAST) { if (me->n % 5 == 0) platform_led_toggle(); }
            else platform_marker((me->n % 20) == 0);
        }
        meas_step(AO_LED, t0);
        return Q_HANDLED();
    }
    case CMD_SIG: if (Q_EVT_CAST(CmdEvt)->code == CMD_PERIOD) ao_period_arm(&me->period, AO_LED, (uint32_t)Q_EVT_CAST(CmdEvt)->a); return Q_HANDLED();
    default: return Q_SUPER(&QHsm_top);
    }
}

/* ================= tick: рядок стану раз на секунду (той самий формат, що в ЛР3–ЛР4) ================= */
typedef struct { QActive super; QTimeEvt period; uint32_t seconds; uint16_t q[4]; int32_t ex, ey; uint8_t mode, sky; } Tick;
static Tick l_tick;
QActive * const AO_Tick = &l_tick.super;
static QState Tick_initial(Tick *me, void const *par);
static QState Tick_reporting(Tick *me, QEvt const *e);

void Tick_ctor(void)
{
    QActive_ctor(&l_tick.super, Q_STATE_CAST(&Tick_initial));
    QTimeEvt_ctorX(&l_tick.period, &l_tick.super, PERIOD_SIG, 0U);
}
static QState Tick_initial(Tick *me, void const *par)
{
    Q_UNUSED_PAR(par);
    me->mode = MODE_OVERCAST; me->sky = SKY_OVERCAST;
    QActive_subscribe(&me->super, TRACK_SIG);
    QActive_subscribe(&me->super, SKY_SIG);
    ao_period_arm(&me->period, AO_TICK, 1000U);
    return Q_TRAN(&Tick_reporting);
}
static QState Tick_reporting(Tick *me, QEvt const *e)
{
    switch (e->sig) {
    case TRACK_SIG: {
        TrackEvt const *t = Q_EVT_CAST(TrackEvt);
        memcpy(me->q, t->q, sizeof me->q); me->mode = t->mode;
        quadrant_error_t er = quadrant_error(t->q); me->ex = er.x_permille; me->ey = er.y_permille;
        return Q_HANDLED();
    }
    case SKY_SIG: me->sky = Q_EVT_CAST(SkyEvt)->sky; return Q_HANDLED();
    case PERIOD_SIG: {
        uint32_t t0 = platform_cycles();
        if (ao_period(&me->period, AO_TICK, t0)) {
            me->seconds++;
            uint32_t load = meas_load_update();
            ao_say(&me->super, "tick %lu (uptime %lu s, TL=%u TR=%u BL=%u BR=%u, ex=%ld ey=%ld) mode=%s sky=%s load=%lu%%",
                   (unsigned long)platform_millis(), (unsigned long)me->seconds, me->q[0], me->q[1], me->q[2], me->q[3],
                   (long)me->ex, (long)me->ey, tracker_mode_name((track_mode_t)me->mode), sky_name((sky_t)me->sky), (unsigned long)load);
        }
        meas_step(AO_TICK, t0);
        return Q_HANDLED();
    }
    case CMD_SIG: if (Q_EVT_CAST(CmdEvt)->code == CMD_PERIOD) ao_period_arm(&me->period, AO_TICK, (uint32_t)Q_EVT_CAST(CmdEvt)->a); return Q_HANDLED();
    default: return Q_SUPER(&QHsm_top);
    }
}
