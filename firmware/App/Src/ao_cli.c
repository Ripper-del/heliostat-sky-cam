/* ЛР5: AO cli (задача cli у ЛР4) — Console, ЄДИНИЙ власник UART TX (у ЛР4 — м'ютекс printf).
 * Плоска машина з одним станом ready:
 *   LINE — рядок команди з переривання UART (уже зібраний у ISR): розбір тим самим cli.c, що в ЛР3–ЛР4;
 *   TEXT — рядок від іншого AO: друк. Інші AO в UART не пишуть.
 * Команди, що читають або змінюють дані іншого AO, не лізуть у його поля: cli надсилає власнику CMD, власник відповідає TEXT.
 * Тут же product_*(): той самий інтерфейс, через який cli.c працював у ЛР3–ЛР4. */
#include <stdio.h>
#include <string.h>
#include "ao.h"
#include "meas.h"
#include "cli.h"
#include "product.h"
#include "platform.h"

typedef struct { QActive super; QTimeEvt test; uint32_t eol_cyc; } Cli;
static Cli l_cli;
QActive * const AO_Cli = &l_cli.super;

static QState Cli_initial(Cli *me, void const *par);
static QState Cli_ready(Cli *me, QEvt const *e);

void Cli_ctor(void)
{
    QActive_ctor(&l_cli.super, Q_STATE_CAST(&Cli_initial));
    QTimeEvt_ctorX(&l_cli.test, &l_cli.super, TEST_STEP_SIG, 0U);
}

void Cli_test_arm(uint32_t ms) { (void)QTimeEvt_disarm(&l_cli.test); QTimeEvt_armX(&l_cli.test, MS(ms), 0U); }

static QState Cli_initial(Cli *me, void const *par)
{
    Q_UNUSED_PAR(par);
    return Q_TRAN(&Cli_ready);
}

static QState Cli_ready(Cli *me, QEvt const *e)
{
    switch (e->sig) {
    case LINE_SIG: {
        uint32_t t0 = platform_cycles();
        LineEvt const *l = Q_EVT_CAST(LineEvt);
        me->eol_cyc = l->eol_cyc;
        if (l->n == 1 && !(l->s[0] >= 'A' && l->s[0] <= 'Z')) cli_handle(l->s[0]);       /* односимвольна команда ЛР2 */
        else { for (uint8_t i = 0; i < l->n; i++) cli_feed(l->s[i]); cli_feed('\r'); }
        meas_step(AO_CLI, t0);
        return Q_HANDLED();
    }
    case TEST_STEP_SIG: {                              /* TEST AUTO: крок прогону сценаріїв (тестовий код, Core/Src/qp_tests.c) */
        uint32_t next = meas_test_step();
        if (next) QTimeEvt_armX(&me->test, MS(next), 0U);
        return Q_HANDLED();
    }
    case TEXT_SIG: {
        uint32_t t0 = platform_cycles();
        printf("%s\r\n", Q_EVT_CAST(TextEvt)->s);
        meas_step(AO_CLI, t0);
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&QHsm_top);
    }
}

/* ================= product_*: команди cli.c як події власникам ================= */
static void post_cmd(QActive *to, uint8_t code, int32_t a, int32_t b, char const *text)
{
    CmdEvt *c = Q_NEW_X(CmdEvt, 2U, CMD_SIG);
    if (!c) { g_ev_lost++; printf("[cli] busy: no event memory\r\n"); return; }
    c->code = code; c->a = a; c->b = b; c->text[0] = 0;
    if (text) { strncpy(c->text, text, sizeof c->text - 1U); c->text[sizeof c->text - 1U] = 0; }
    if (!QACTIVE_POST_X(to, &c->super, 1U, &l_cli.super)) { g_ev_lost++; printf("[cli] busy: queue full\r\n"); }
}

void product_print_stats(void)  { meas_print_stats(); }
void product_reset_stats(void)  { meas_reset_stats(); }
void product_print_mode(void)   { post_cmd(AO_Control, CMD_MODE, 0, 0, 0); }
void product_print_sky(void)    { post_cmd(AO_Camera, CMD_SKY, 0, 0, 0); }
void product_print_mem(void)    { meas_print_mem(); }
void product_cal_show(void)     { post_cmd(AO_Control, CMD_CAL, 0, 0, "SHOW"); }
int  product_cal_cmd(int argc, char **argv)
{
    char t[40]; size_t k = 0;
    for (int i = 0; i < argc && k < sizeof t - 1U; i++) {
        if (i) t[k++] = ' ';
        for (const char *s = argv[i]; *s && k < sizeof t - 1U; s++) t[k++] = *s;
    }
    t[k] = 0;
    post_cmd(AO_Control, CMD_CAL, 0, 0, t);
    return PRODUCT_ASYNC;
}
void product_log_dump(int n)    { post_cmd(AO_Log, CMD_LOG_DUMP, n, 0, 0); }
int  product_log_erase(void)    { post_cmd(AO_Log, CMD_LOG_ERASE, 0, 0, 0); return PRODUCT_ASYNC; }
void product_frames_info(void)  { post_cmd(AO_Log, CMD_FRAMES_INFO, 0, 0, 0); }
int  product_frame_show(uint32_t no) { post_cmd(AO_Camera, CMD_FRAME_SHOW, (int32_t)no, 0, 0); return 0; }
void product_snapshot_request(void) { ao_snap(&l_cli.super); }
void product_sky_force(int v)   { post_cmd(AO_Camera, CMD_SKY_FORCE, v, 0, 0); }
void product_servo_power(int on){ post_cmd(AO_Control, CMD_SERVO, on, 0, 0); }
int  product_cam_exposure(uint32_t aec, uint32_t gain) { post_cmd(AO_Camera, CMD_CAM_EXP, (int32_t)aec, (int32_t)gain, 0); return PRODUCT_ASYNC; }
void product_print_version(void){ meas_print_version(); }
void product_probe_test(void)   { meas_probe_test(); }
uint8_t *product_frame_buf(void){ return Camera_service_buf(); }
void product_cmd_started(void)  { meas_add(M_CMD, platform_cycles() - l_cli.eol_cyc); }   /* Enter у ISR -> початок виконання команди */
void product_print_tasks(void)  { meas_print_tasks(); }
void product_print_stack(void)  { meas_print_stack(); }
void product_print_heap(void)   { meas_print_heap(); }
void product_print_oscost(void) { meas_print_oscost(); }
void product_print_qstat(void)  { meas_print_qstat(); }
void product_trace_cmd(int argc, char **argv) { meas_trace_cmd(argc, argv); }
void product_btnsim(uint32_t period_ms) { platform_btnsim(period_ms); }
void product_cam_irq_mode(int mode) { platform_cam_set_irq_mode(mode); }

int product_set_period(const char *name, uint32_t ms)
{
    if (ms < 1 || ms > 600000) return -1;
    for (int i = 0; i < AO_COUNT; i++)
        if (!strcmp(g_rt[i].name, name) && g_rt[i].period_ms) { post_cmd(g_rt[i].ao, CMD_PERIOD, (int32_t)ms, 0, 0); return 0; }
    return -1;
}
