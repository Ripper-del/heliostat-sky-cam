/* ЛР5: AO button (задача button у ЛР4). Найвищий пріоритет: реакція на кнопку B1 (FR-11). Одна подія — один короткий крок. */
#include "ao.h"
#include "meas.h"
#include "platform.h"

typedef struct { QActive super; uint32_t starve_end; } Button;
static Button l_button;
QActive * const AO_Button = &l_button.super;

static QState Button_initial(Button *me, void const *par);
static QState Button_ready(Button *me, QEvt const *e);

void Button_ctor(void) { QActive_ctor(&l_button.super, Q_STATE_CAST(&Button_initial)); }

static QState Button_initial(Button *me, void const *par)
{
    Q_UNUSED_PAR(par);
    return Q_TRAN(&Button_ready);
}

static QState Button_ready(Button *me, QEvt const *e)
{
    switch (e->sig) {
    case BUTTON_SIG: {
        uint32_t t0 = platform_cycles();
        platform_probe(4, 1);                                          /* початок реакції (аналізатор, D1) */
        BtnEvt const *b = Q_EVT_CAST(BtnEvt);
        meas_add(M_BTN, t0 - b->isr_cyc);                              /* ISR -> початок реакції */
        meas_add(M_EVT, t0 - b->evt_cyc);                              /* подія -> початок реакції */
        g_btn_presses++;
        ao_snap(&me->super);
        platform_probe(4, 0);
        meas_step(AO_BUTTON, t0);
        return Q_HANDLED();
    }
    case CMD_SIG: {                                    /* сценарій «голодування»: AO найвищого пріоритету шле подію сам собі */
        CmdEvt const *c = Q_EVT_CAST(CmdEvt);
        if (c->code != CMD_FAIL_STEP) return Q_HANDLED();
        if (c->a) me->starve_end = platform_millis() + (uint32_t)c->a;
        meas_spin_ms(1);
        if ((int32_t)(platform_millis() - me->starve_end) < 0) {
            CmdEvt *n = Q_NEW_X(CmdEvt, 2U, CMD_SIG);
            if (n) { n->code = CMD_FAIL_STEP; n->a = 0; QACTIVE_POST(&me->super, &n->super, &me->super); }
        } else ao_say(&me->super, "[test] starve done");
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&QHsm_top);
    }
}
