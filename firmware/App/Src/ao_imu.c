/* ЛР5: AO imu (задача imu у ЛР4). Власник I2C1 і MPU-6050, період 100 мс.
 * Два стани: ok і fault. Після трьох помилок поспіль — fault (датчик відключено або шина в аварії); у fault той самий період,
 * але спершу будимо датчик (після повернення живлення він спить), перший успішний обмін — назад в ok.
 * У ЛР4 помилки лише рахувались; стани додано для сценарію п. 9.3.7 «відмова сенсора». */
#include "ao.h"
#include "meas.h"
#include "platform.h"
#include "mpu6050.h"

typedef struct { QActive super; QTimeEvt period; mpu6050_t dev; mpu6050_raw_t raw; uint8_t fails; uint32_t fault_ms; } Imu;
static Imu l_imu;
QActive * const AO_Imu = &l_imu.super;

static QState Imu_initial(Imu *me, void const *par);
static QState Imu_polling(Imu *me, QEvt const *e);
static QState Imu_ok(Imu *me, QEvt const *e);
static QState Imu_fault(Imu *me, QEvt const *e);

void Imu_ctor(void)
{
    QActive_ctor(&l_imu.super, Q_STATE_CAST(&Imu_initial));
    QTimeEvt_ctorX(&l_imu.period, &l_imu.super, PERIOD_SIG, 0U);
}

static QState Imu_initial(Imu *me, void const *par)
{
    Q_UNUSED_PAR(par);
    mpu6050_init_dev(&me->dev, platform_i2c1());        /* датчик уже розбуджено в bsp до старту (як у ЛР4) */
    ao_period_arm(&me->period, AO_IMU, 100U);
    return Q_TRAN(&Imu_ok);
}

static QState Imu_polling(Imu *me, QEvt const *e)
{
    switch (e->sig) {
    case CMD_SIG: if (Q_EVT_CAST(CmdEvt)->code == CMD_PERIOD) ao_period_arm(&me->period, AO_IMU, (uint32_t)Q_EVT_CAST(CmdEvt)->a); return Q_HANDLED();
    default: return Q_SUPER(&QHsm_top);
    }
}

static QState Imu_ok(Imu *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG: me->fails = 0; return Q_HANDLED();
    case PERIOD_SIG: {
        uint32_t t0 = platform_cycles();
        int bad = 0;
        if (ao_period(&me->period, AO_IMU, t0)) {
            if (mpu6050_read_accel_raw(&me->dev, &me->raw) != DEV_OK) { g_imu_err++; bad = 1; }
            else me->fails = 0;
        }
        meas_step(AO_IMU, t0);
        if (bad && ++me->fails >= 3U) return Q_TRAN(&Imu_fault);
        return Q_HANDLED();
    }
    default: return Q_SUPER(&Imu_polling);
    }
}

static QState Imu_fault(Imu *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG:
        me->fault_ms = platform_millis();
        ao_say(&me->super, "[imu] fault: 3 errors in a row");
        return Q_HANDLED();
    case PERIOD_SIG: {
        uint32_t t0 = platform_cycles();
        int ok = 0;
        if (ao_period(&me->period, AO_IMU, t0)) {
            (void)mpu6050_wake(&me->dev);
            if (mpu6050_read_accel_raw(&me->dev, &me->raw) == DEV_OK) ok = 1; else g_imu_err++;
        }
        meas_step(AO_IMU, t0);
        if (ok) {
            ao_say(&me->super, "[imu] recovered after %lu ms", (unsigned long)(platform_millis() - me->fault_ms));
            return Q_TRAN(&Imu_ok);
        }
        return Q_HANDLED();
    }
    default: return Q_SUPER(&Imu_polling);
    }
}
