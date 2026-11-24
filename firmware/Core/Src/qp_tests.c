/**
 * @file qp_tests.c — ЛР5: сценарії п. 9.2–9.3 методички (команда TEST ...). Тестовий код: тут можна FreeRTOS.
 *   TEST OFF            — вимкнути всі сценарії
 *   TEST HOG ms         — imu (середній пріоритет) займає процесор ms кожні 100 мс (інверсія пріоритетів, п. 9.2)
 *   TEST LOGRATE ms     — control пише запис журналу кожні ms (багато вимірів flash_wait, п. 9.3.1)
 *   TEST BLOCK 1|0      — log чекає flash з блокуванням в обробнику замість POLL (п. 9.3.2)
 *   TEST OWNER ms       — log ще й тримає процесор ms у кожному кроці (п. 9.3.2, «Bus чекає 5 мс»)
 *   TEST CTRL ms        — control робить ms роботи на кожному SAMPLE (довгий крок без блокування, п. 9.3.3)
 *   TEST ONESTEP 1|0    — log пише весь кадр одним кроком (п. 9.3.3)
 *   TEST SNAPTO ms      — таймаут знімка коротший за саму операцію (неочікувана подія, п. 9.3.6)
 *   TEST FLASHFAIL 1|0  — кожна операція flash повертає помилку шини (збій шини, п. 9.3.8)
 *   TEST STARVE ms      — button (найвищий пріоритет) шле сам собі подію ms (голодування, п. 9.2)
 *   TEST POOL           — брати події з пулу без повернення: Q_NEW -> Q_onError qf_dyn (п. 9.3.5)
 *   TEST STATUS         — поточні перемикачі і запізнілі події
 *   TEST AUTO           — прошивка сама проганяє всі сценарії підряд (навантаження: BTNSIM 700 і знімок кожні 3,2 с)
 *                         і після кожного друкує STATS і QSTAT; хост лише читає (див. проблему 36 у docs/problems.md)
 */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "ao.h"
#include "meas.h"
#include "product.h"
#include "platform.h"

meas_test_t g_test;

void meas_block_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); }

void meas_spin_ms(uint32_t ms)
{
    uint32_t t0 = platform_cycles(), n = ms * (platform_cpu_hz() / 1000U);
    while (platform_cycles() - t0 < n) { }
}

/* ---- варіант LR5_FLASH_MUTEX (п. 9.3.1): м'ютекс flash і очікування драйвера з блокуванням задачі, як у ЛР4 ---- */
static SemaphoreHandle_t flash_mx;
void meas_flash_lock(void)   { if (!flash_mx) flash_mx = xSemaphoreCreateMutex(); xSemaphoreTake(flash_mx, portMAX_DELAY); }
void meas_flash_unlock(void) { xSemaphoreGive(flash_mx); }
void meas_delay_hook(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); else HAL_Delay(ms);
}

/* ---- TEST AUTO: послідовність сценаріїв, крок за кроком із часової події cli ---- */
typedef struct { char const *name; uint16_t sec; uint8_t keep_stats; void (*set)(void); } auto_t;
static void a_none(void)    { }
static void a_lograte(void) { g_test.log_every_ms = 50; }
static void a_inv(void)     { g_test.log_every_ms = 50; g_test.hog_ms = 50; }
static __attribute__((unused)) void a_block(void)   { g_test.log_every_ms = 50; g_test.block_log = 1; }
static __attribute__((unused)) void a_owner(void)   { g_test.log_every_ms = 50; g_test.owner_wait_ms = 5; }
static __attribute__((unused)) void a_ctrl(void)    { g_test.ctrl_work_ms = 30; }
static __attribute__((unused)) void a_onestep(void) { g_test.log_every_ms = 50; g_test.one_step_save = 1; }
static __attribute__((unused)) void a_starve(void)
{
    CmdEvt *c = Q_NEW_X(CmdEvt, 2U, CMD_SIG);
    if (c) { c->code = CMD_FAIL_STEP; c->a = 2000; if (!QACTIVE_POST_X(AO_Button, &c->super, 1U, 0)) { } }
}
static __attribute__((unused)) void a_snapto(void)  { g_test.snap_timeout_ms = 1000; }
static __attribute__((unused)) void a_ffon(void)    { g_test.log_every_ms = 100; g_test.flash_fail = 1; }
static __attribute__((unused)) void a_ffoff(void)   { g_test.log_every_ms = 100; g_test.flash_fail = 0; }
static const auto_t autos[] = {
    { "base",    30, 0, a_none },
    { "lograte", 30, 0, a_lograte },
    { "inv",     30, 0, a_inv },
#ifndef LR5_FLASH_MUTEX
    { "block",   30, 0, a_block },
    { "owner5",  30, 0, a_owner },
    { "ctrl30",  30, 0, a_ctrl },
    { "onestep", 30, 0, a_onestep },
    { "starve",   5, 0, a_starve },
    { "snapto",  15, 0, a_snapto },
    { "flashfail_on",  3, 0, a_ffon },
    { "flashfail_off", 6, 1, a_ffoff },
#endif
};
static unsigned a_idx, a_phase;
uint32_t meas_test_step(void)
{
    if (a_idx >= sizeof autos / sizeof autos[0]) {
        memset((void *)&g_test, 0, sizeof g_test); platform_btnsim(0);
        printf("[auto] all done\r\n");
        return 0;
    }
    auto_t const *a = &autos[a_idx];
    if (a_phase == 0) {
        uint32_t late = g_test.late_events;
        memset((void *)&g_test, 0, sizeof g_test); g_test.late_events = a->keep_stats ? late : 0;
        g_test.snap_every_ms = 3200; platform_btnsim(700);
        a->set();
        if (!a->keep_stats) meas_reset_stats();
        printf("[auto] begin %s %u s\r\n", a->name, (unsigned)a->sec);
        a_phase = 1;
        return a->sec * 1000U;
    }
    a_phase = 0; a_idx++;
    if (a_idx < sizeof autos / sizeof autos[0] && autos[a_idx].keep_stats) return 1;   /* продовження того самого виміру */
    printf("[auto] end %s\r\n", a->name);
    meas_print_stats();
    meas_print_qstat();
    printf("[test] hog=%u lograte=%u block=%u owner=%u ctrl=%u onestep=%u snapto=%lu flashfail=%u late_events=%lu\r\n",
           g_test.hog_ms, g_test.log_every_ms, g_test.block_log, g_test.owner_wait_ms, g_test.ctrl_work_ms, g_test.one_step_save,
           (unsigned long)g_test.snap_timeout_ms, g_test.flash_fail, (unsigned long)g_test.late_events);
    return 2000;
}

static uint32_t num(char const *s) { uint32_t v = 0; for (; s && *s >= '0' && *s <= '9'; s++) v = v * 10 + (uint32_t)(*s - '0'); return v; }

int product_test(int argc, char **argv)
{
    if (argc < 1) return -1;
    char const *t = argv[0]; uint32_t v = argc > 1 ? num(argv[1]) : 0;
    if (!strcmp(t, "OFF")) { memset((void *)&g_test, 0, sizeof g_test); printf("[test] off\r\n"); return 0; }
    if (!strcmp(t, "HOG"))       { g_test.hog_ms = (uint16_t)v; }
    else if (!strcmp(t, "LOGRATE")) { g_test.log_every_ms = (uint16_t)v; }
    else if (!strcmp(t, "BLOCK"))   { g_test.block_log = (uint8_t)(v != 0); }
    else if (!strcmp(t, "OWNER"))   { g_test.owner_wait_ms = (uint16_t)v; }
    else if (!strcmp(t, "CTRL"))    { g_test.ctrl_work_ms = (uint16_t)v; }
    else if (!strcmp(t, "ONESTEP")) { g_test.one_step_save = (uint8_t)(v != 0); }
    else if (!strcmp(t, "SNAPTO"))  { g_test.snap_timeout_ms = v; }
    else if (!strcmp(t, "FLASHFAIL")) { g_test.flash_fail = (uint8_t)(v != 0); }
    else if (!strcmp(t, "STARVE")) {
        CmdEvt *c = Q_NEW_X(CmdEvt, 2U, CMD_SIG);
        if (!c) return -1;
        c->code = CMD_FAIL_STEP; c->a = (int32_t)v;
        if (!QACTIVE_POST_X(AO_Button, &c->super, 1U, 0)) return -1;
        printf("[test] starve %lu ms\r\n", (unsigned long)v);
        return 0;
    }
    else if (!strcmp(t, "AUTO")) {
        a_idx = 0; a_phase = 0;
        printf("[auto] %u scenarios\r\n", (unsigned)(sizeof autos / sizeof autos[0]));
        Cli_test_arm(meas_test_step());
        return 0;
    }
    else if (!strcmp(t, "POOL")) {
        printf("[test] pool1 free=%u min=%u: Q_NEW without return until Q_onError\r\n", (unsigned)QF_getPoolFree(1), (unsigned)QF_getPoolMin(1));
        for (;;) { (void)Q_NEW(BtnEvt, BUTTON_SIG); }        /* блоки не повертаються: пул вичерпається, QP викличе Q_onError */
    }
    else if (strcmp(t, "STATUS")) return -1;
    printf("[test] hog=%u lograte=%u block=%u owner=%u ctrl=%u onestep=%u snapto=%lu flashfail=%u late_events=%lu\r\n",
           g_test.hog_ms, g_test.log_every_ms, g_test.block_log, g_test.owner_wait_ms, g_test.ctrl_work_ms, g_test.one_step_save,
           (unsigned long)g_test.snap_timeout_ms, g_test.flash_fail, (unsigned long)g_test.late_events);
    return 0;
}
