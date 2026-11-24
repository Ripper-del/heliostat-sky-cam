/* ЛР5: AO adc (задача adc у ЛР4). Власник АЦП: кожні 10 мс читає квадрант і надсилає відлік control. */
#include "ao.h"
#include "meas.h"
#include "platform.h"

typedef struct { QActive super; QTimeEvt period; } Adc;
static Adc l_adc;
QActive * const AO_Adc = &l_adc.super;

static QState Adc_initial(Adc *me, void const *par);
static QState Adc_sampling(Adc *me, QEvt const *e);

void Adc_ctor(void)
{
    QActive_ctor(&l_adc.super, Q_STATE_CAST(&Adc_initial));
    QTimeEvt_ctorX(&l_adc.period, &l_adc.super, PERIOD_SIG, 0U);
}

static QState Adc_initial(Adc *me, void const *par)
{
    Q_UNUSED_PAR(par);
    ao_period_arm(&me->period, AO_ADC, 10U);
    return Q_TRAN(&Adc_sampling);
}

static QState Adc_sampling(Adc *me, QEvt const *e)
{
    switch (e->sig) {
    case PERIOD_SIG: {
        uint32_t t0 = platform_cycles();
        if (ao_period(&me->period, AO_ADC, t0)) {
            QuadEvt *s = Q_NEW_X(QuadEvt, 2U, SAMPLE_SIG);
            if (s) {
                platform_probe(0, 1);
                platform_adc_read_quadrant(s->q);
                platform_probe(0, 0);
                if (!QACTIVE_POST_X(AO_Control, &s->super, 1U, &me->super)) g_ev_lost++;
            } else g_ev_lost++;
        }
        meas_step(AO_ADC, t0);
        return Q_HANDLED();
    }
    case CMD_SIG:
        if (Q_EVT_CAST(CmdEvt)->code == CMD_PERIOD) ao_period_arm(&me->period, AO_ADC, (uint32_t)Q_EVT_CAST(CmdEvt)->a);
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}
