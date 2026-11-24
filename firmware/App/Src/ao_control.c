/* ЛР5: AO control (задача control у ЛР4). Власник трекера і калібровки: кожні 10 мс оновлює режим за останнім відліком
 * квадранта, надсилає цілі servo, раз на 100 мс публікує свій стан (TRACK). Калібровку у flash зберігає не сам, а через log. */
#include <string.h>
#include "ao.h"
#include "meas.h"
#include "platform.h"
#include "evlog.h"
#include "calib.h"

typedef struct {
    QActive  super;
    QTimeEvt period;
    tracker_t trk;
    uint16_t q[4];
    sky_t    sky;
    uint8_t  flash_ok, n;
    uint32_t last_suspect_ms;
} Control;
static Control l_control;
QActive * const AO_Control = &l_control.super;

static QState Control_initial(Control *me, void const *par);
static QState Control_running(Control *me, QEvt const *e);

void Control_ctor(void)
{
    QActive_ctor(&l_control.super, Q_STATE_CAST(&Control_initial));
    QTimeEvt_ctorX(&l_control.period, &l_control.super, PERIOD_SIG, 0U);
}

static QState Control_initial(Control *me, void const *par)
{
    BootInfo const *b = (BootInfo const *)par;
    tracker_init(&me->trk, &b->cfg);
    me->flash_ok = b->flash_ok;
    me->sky = SKY_OVERCAST;
    platform_adc_read_quadrant(me->q);
    QActive_subscribe(&me->super, SKY_SIG);
    ao_period_arm(&me->period, AO_CONTROL, 10U);
    return Q_TRAN(&Control_running);
}

static void publish_track(Control *me)
{
    TrackEvt *t = Q_NEW_X(TrackEvt, 4U, TRACK_SIG);
    if (!t) { g_ev_lost++; return; }
    memcpy(t->q, me->q, sizeof t->q);
    t->light = me->trk.light; t->dark_thr = me->trk.cfg.dark_light_thr; t->ex = me->trk.ex; t->ey = me->trk.ey; t->mode = (uint8_t)me->trk.mode;
    QACTIVE_PUBLISH(&t->super, &me->super);
}

static void post_servo(Control *me, uint8_t sig, uint8_t on)
{
    TargetEvt *t = Q_NEW_X(TargetEvt, 2U, sig);
    if (!t) { g_ev_lost++; return; }
    t->az = me->trk.az_us; t->el = me->trk.el_us; t->on = on;
    if (!QACTIVE_POST_X(AO_Servo, &t->super, 1U, &me->super)) g_ev_lost++;
}

static int parse_u(const char *s, uint32_t *out)
{
    if (!s || !*s) return -1;
    uint32_t v = 0;
    for (; *s; s++) { if (*s < '0' || *s > '9') return -1; v = v * 10 + (uint32_t)(*s - '0'); }
    *out = v; return 0;
}

/* CAL ...: та сама логіка, що cal_cmd_locked() у ЛР4. 0 — готово, 1 — відповідь прийде пізніше (SAVE/LOAD через log), -1 — помилка. */
static int cal_cmd(Control *me, char *text)
{
    char *argv[6]; int argc = 0;
    for (char *p = text; *p && argc < 6; ) { while (*p == ' ') *p++ = 0; if (!*p) break; argv[argc++] = p; while (*p && *p != ' ') p++; }
    tracker_t *trk = &me->trk; tracker_cfg_t *c = &trk->cfg;
    if (argc < 1) return -1;
    if (!strcmp(argv[0], "SHOW")) {
        ao_say(&me->super, "[cal] AZ %u..%u us, EL %u..%u us, park %u/%u, dark_thr=%lu, invert=%d, sign az=%d el=%d, zero ex=%d ey=%d", c->az_min_us, c->az_max_us,
               c->el_min_us, c->el_max_us, c->park_az_us, c->park_el_us, (unsigned long)c->dark_light_thr, c->invert_light, c->sign_az, c->sign_el, c->ex_off, c->ey_off);
        return 2;
    }
    if (!strcmp(argv[0], "AZ") || !strcmp(argv[0], "EL")) {
        uint32_t lo, hi;
        if (argc < 3 || parse_u(argv[1], &lo) || parse_u(argv[2], &hi) || calib_check_range((uint16_t)lo, (uint16_t)hi)) return -1;
        if (argv[0][0] == 'A') { c->az_min_us = (uint16_t)lo; c->az_max_us = (uint16_t)hi; }
        else { c->el_min_us = (uint16_t)lo; c->el_max_us = (uint16_t)hi; }
        trk->az_us = trk->az_us < c->az_min_us ? c->az_min_us : trk->az_us > c->az_max_us ? c->az_max_us : trk->az_us;
        trk->el_us = trk->el_us < c->el_min_us ? c->el_min_us : trk->el_us > c->el_max_us ? c->el_max_us : trk->el_us;
        return 0;
    }
    if (!strcmp(argv[0], "ZERO")) { c->ex_off = (int16_t)(trk->ex + c->ex_off); c->ey_off = (int16_t)(trk->ey + c->ey_off); return 0; }
    if (!strcmp(argv[0], "DEADBAND")) { uint32_t v; if (argc < 2 || parse_u(argv[1], &v) || v > 500) return -1; c->deadband_permille = (uint16_t)v; return 0; }
    if (!strcmp(argv[0], "DARK")) { uint32_t v; if (argc < 2 || parse_u(argv[1], &v)) return -1; c->dark_light_thr = v; return 0; }
    if (!strcmp(argv[0], "INV"))  { uint32_t v; if (argc < 2 || parse_u(argv[1], &v) || v > 1) return -1; c->invert_light = (int8_t)v; return 0; }
    if (!strcmp(argv[0], "SIGN")) {
        if (argc < 3 || (strcmp(argv[2], "+1") && strcmp(argv[2], "-1"))) return -1;
        int8_t v = argv[2][0] == '-' ? -1 : 1;
        if (!strcmp(argv[1], "AZ")) c->sign_az = v; else if (!strcmp(argv[1], "EL")) c->sign_el = v; else return -1;
        return 0;
    }
    if (!strcmp(argv[0], "SAVE") || !strcmp(argv[0], "LOAD")) {
        if (!me->flash_ok) return -1;
        int load = argv[0][0] == 'L';
        CalEvt *ce = Q_NEW_X(CalEvt, 2U, load ? CAL_LOAD_SIG : CAL_SAVE_SIG);
        if (!ce) return -1;
        ce->cfg = *c; ce->load = (uint8_t)load; ce->rc = 0;
        if (!QACTIVE_POST_X(AO_Log, &ce->super, 1U, &me->super)) return -1;
        return 1;
    }
    if (!strcmp(argv[0], "DEFAULT")) { tracker_default_cfg(c); return 0; }
    return -1;
}

static QState Control_running(Control *me, QEvt const *e)
{
    switch (e->sig) {
    case SAMPLE_SIG:                                   /* останній відлік; режим рахується за періодом, як у ЛР4 */
        memcpy(me->q, Q_EVT_CAST(QuadEvt)->q, sizeof me->q);
        return Q_HANDLED();
    case SKY_SIG: {                                    /* клас неба від camera (FR-13) і перевірка «датчик бреше» */
        SkyEvt const *s = Q_EVT_CAST(SkyEvt);
        me->sky = (sky_t)s->sky;
        int ldr_dark = me->trk.light < me->trk.cfg.dark_light_thr;
        uint32_t now = platform_millis();
        if (sky_mismatch(me->sky, ldr_dark) && now - me->last_suspect_ms >= 10000U) {
            me->last_suspect_ms = now;
            ao_log(&me->super, EV_SENSOR_SUSPECT, EVLOG_NO_FRAME, s->mean, (int16_t)(me->trk.light / 4), (int16_t)me->sky);
        }
        return Q_HANDLED();
    }
    case PERIOD_SIG: {
        uint32_t t0 = platform_cycles();
        if (ao_period(&me->period, AO_CONTROL, t0)) {
            track_mode_t before = me->trk.mode;
            if (tracker_update(&me->trk, me->q, me->sky, platform_millis())) {
                ao_log(&me->super, EV_MODE, EVLOG_NO_FRAME, (int16_t)before, (int16_t)me->trk.mode, (int16_t)(me->trk.light / 4));
                if (me->trk.mode == MODE_PARK) ao_snap(&me->super);              /* FR-14: вхід у PARK = знімок */
            }
            post_servo(me, TARGET_SIG, 0U);
            if (++me->n >= 10U) { me->n = 0; publish_track(me); }
        }
        meas_step(AO_CONTROL, t0);
        return Q_HANDLED();
    }
    case CAL_DONE_SIG: {                               /* відповідь log на CAL SAVE / CAL LOAD */
        CalEvt const *ce = Q_EVT_CAST(CalEvt);
        if (ce->rc == 0 && ce->load) me->trk.cfg = ce->cfg;
        if (ce->rc == 0 && !ce->load) ao_log(&me->super, EV_CAL, EVLOG_NO_FRAME, 0, 0, 0);
        ao_say(&me->super, "[cal] %s", ce->rc == 0 ? "ok" : "ERROR (see HELP)");
        return Q_HANDLED();
    }
    case CMD_SIG: {
        CmdEvt const *c = Q_EVT_CAST(CmdEvt);
        if (c->code == CMD_MODE) {
            tracker_t const *t = &me->trk;
            ao_say(&me->super, "[prod] mode=%s light=%lu thr=%lu ex=%ld ey=%ld az=%u el=%u", tracker_mode_name(t->mode),
                   (unsigned long)t->light, (unsigned long)t->cfg.dark_light_thr, (long)t->ex, (long)t->ey, t->az_us, t->el_us);
        } else if (c->code == CMD_CAL) {
            char buf[sizeof c->text]; memcpy(buf, c->text, sizeof buf); buf[sizeof buf - 1] = 0;
            int rc = cal_cmd(me, buf);
            if (rc == 0 || rc == -1) ao_say(&me->super, "[cal] %s", rc == 0 ? "ok" : "ERROR (see HELP)");
        } else if (c->code == CMD_SERVO) {
            if (c->a) { me->trk.az_us = me->trk.cfg.park_az_us; me->trk.el_us = me->trk.cfg.park_el_us; }
            post_servo(me, SERVO_PWR_SIG, (uint8_t)(c->a != 0));
        } else if (c->code == CMD_PERIOD) ao_period_arm(&me->period, AO_CONTROL, (uint32_t)c->a);
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&QHsm_top);
    }
}
