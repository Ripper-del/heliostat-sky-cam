/* ЛР5: невеликі допоміжні функції, спільні для AO. Лише API QP: створити подію і надіслати її власнику. */
#include <stdarg.h>
#include <stdio.h>
#include "ao.h"
#include "meas.h"
#include "platform.h"

Q_DEFINE_THIS_MODULE("ao_util")

/* У UART пише лише cli. Інші AO надсилають рядок подією; якщо пул або черга cli повні — рядок втрачається з лічильником. */
void ao_say(QActive const *sender, char const *fmt, ...)
{
    Q_UNUSED_PAR(sender);                              /* відправник потрібен лише трасуванню QS */
    TextEvt *t = Q_NEW_X(TextEvt, 2U, TEXT_SIG);
    if (!t) { g_text_lost++; return; }
    va_list ap; va_start(ap, fmt);
    vsnprintf(t->s, sizeof t->s, fmt, ap);
    va_end(ap);
    if (!QACTIVE_POST_X(AO_Cli, &t->super, 2U, sender)) g_text_lost++;
}

void ao_log(QActive const *sender, uint8_t type, uint8_t frame, int16_t a, int16_t b, int16_t c)
{
    Q_UNUSED_PAR(sender);
    LogEvt *l = Q_NEW_X(LogEvt, 2U, LOG_REQ_SIG);
    if (!l) { g_pend_lost++; return; }
    l->type = type; l->frame = frame; l->a = a; l->b = b; l->c = c;
    l->t_ms = platform_millis(); l->post_cyc = platform_cycles();
    if (!QACTIVE_POST_X(AO_Log, &l->super, 2U, sender)) g_pend_lost++;   /* черга повна: подію втрачено, але видно в лічильнику (як у ЛР4) */
}

void ao_snap(QActive const *sender)
{
    Q_UNUSED_PAR(sender);
    static QEvt const snapEvt = QEVT_INITIALIZER(SNAP_REQ_SIG);
    if (!QACTIVE_POST_X(AO_Snapshot, &snapEvt, 1U, sender)) g_ev_lost++;
}

void ao_period_arm(QTimeEvt *te, int id, uint32_t ms)
{
    (void)QTimeEvt_disarm(te);
    g_rt[id].period_ms = ms;
    meas_period_arm(id);
    QTimeEvt_armX(te, MS(ms), 0U);
}

int ao_period(QTimeEvt *te, int id, uint32_t t0)
{
    uint32_t next;
    int work = meas_period_begin(id, t0, &next);
    (void)QTimeEvt_disarm(te);              /* застаріла подія після PERIOD task ms: таймер уже запущено заново */
    QTimeEvt_armX(te, next, 0U);
    return work;
}
