/* ЛР5: AO servo (задача servo у ЛР4). Власник ШІМ серво. Два стани: off (виходи вимкнені) і on (кожні 20 мс крок до цілі).
 * Увімкнення і вимкнення ШІМ — в entry/exit стану on: за будь-якого виходу з on серво гарантовано вимикаються. */
#include "ao.h"
#include "meas.h"
#include "platform.h"
#include "quadrant.h"

typedef struct { QActive super; QTimeEvt period; uint16_t az, el; } Servo;
static Servo l_servo;
QActive * const AO_Servo = &l_servo.super;

static QState Servo_initial(Servo *me, void const *par);
static QState Servo_powered(Servo *me, QEvt const *e);
static QState Servo_off(Servo *me, QEvt const *e);
static QState Servo_on(Servo *me, QEvt const *e);

void Servo_ctor(void)
{
    QActive_ctor(&l_servo.super, Q_STATE_CAST(&Servo_initial));
    QTimeEvt_ctorX(&l_servo.period, &l_servo.super, PERIOD_SIG, 0U);
}

static QState Servo_initial(Servo *me, void const *par)
{
    Q_UNUSED_PAR(par);
    ao_period_arm(&me->period, AO_SERVO, 20U);
    return Q_TRAN(&Servo_off);
}

/* суперстан: те, що однаково в обох станах */
static QState Servo_powered(Servo *me, QEvt const *e)
{
    switch (e->sig) {
    case TARGET_SIG: me->az = Q_EVT_CAST(TargetEvt)->az; me->el = Q_EVT_CAST(TargetEvt)->el; return Q_HANDLED();
    case CMD_SIG: if (Q_EVT_CAST(CmdEvt)->code == CMD_PERIOD) ao_period_arm(&me->period, AO_SERVO, (uint32_t)Q_EVT_CAST(CmdEvt)->a); return Q_HANDLED();
    default: return Q_SUPER(&QHsm_top);
    }
}

static QState Servo_off(Servo *me, QEvt const *e)
{
    switch (e->sig) {
    case PERIOD_SIG: { uint32_t t0 = platform_cycles(); (void)ao_period(&me->period, AO_SERVO, t0); meas_step(AO_SERVO, t0); return Q_HANDLED(); }
    case SERVO_PWR_SIG: {
        TargetEvt const *t = Q_EVT_CAST(TargetEvt);
        if (!t->on) return Q_HANDLED();
        me->az = t->az; me->el = t->el;                                   /* парк-положення від control */
        return Q_TRAN(&Servo_on);
    }
    default: return Q_SUPER(&Servo_powered);
    }
}

static QState Servo_on(Servo *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG:
        platform_servo_set_us(PLATFORM_SERVO_AZ, me->az); platform_servo_set_us(PLATFORM_SERVO_EL, me->el);
        platform_servo_enable(PLATFORM_SERVO_AZ, 1); platform_servo_enable(PLATFORM_SERVO_EL, 1);
        return Q_HANDLED();
    case Q_EXIT_SIG:
        platform_servo_enable(PLATFORM_SERVO_AZ, 0); platform_servo_enable(PLATFORM_SERVO_EL, 0);
        return Q_HANDLED();
    case PERIOD_SIG: {
        uint32_t t0 = platform_cycles();
        if (ao_period(&me->period, AO_SERVO, t0)) {
            platform_servo_set_us(PLATFORM_SERVO_AZ, servo_next_step(platform_servo_get_us(PLATFORM_SERVO_AZ), me->az, 20));
            platform_servo_set_us(PLATFORM_SERVO_EL, servo_next_step(platform_servo_get_us(PLATFORM_SERVO_EL), me->el, 20));
        }
        meas_step(AO_SERVO, t0);
        return Q_HANDLED();
    }
    case SERVO_PWR_SIG:
        return Q_EVT_CAST(TargetEvt)->on ? Q_HANDLED() : Q_TRAN(&Servo_off);
    default: return Q_SUPER(&Servo_powered);
    }
}
