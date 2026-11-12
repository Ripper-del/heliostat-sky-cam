#include "test.h"
#include "tracker.h"

static tracker_t t; static tracker_cfg_t cfg;
static void setup(void) { tracker_default_cfg(&cfg); tracker_init(&t, &cfg); }

static const uint16_t bright_left[4]  = { 3000, 1000, 3000, 1000 };   /* ліві яскравіші: ex < 0 */
static const uint16_t bright_right[4] = { 1000, 3000, 1000, 3000 };
static const uint16_t bright_top[4]   = { 3000, 3000, 1000, 1000 };
static const uint16_t balanced[4]     = { 2000, 2000, 2000, 2000 };
static const uint16_t dark[4]         = { 100, 100, 100, 100 };

static void light_sum_and_polarity(void)
{
    CHECK_EQ(tracker_light(balanced, 0), 8000);
    CHECK_EQ(tracker_light(balanced, 1), 4 * 4095 - 8000);
}

static void track_moves_towards_light_and_clamps(void)
{
    setup();
    tracker_update(&t, bright_right, SKY_CLEAR, 0);
    CHECK_EQ(t.mode, MODE_TRACK); CHECK_EQ(t.az_us, 1510);
    tracker_update(&t, bright_left, SKY_CLEAR, 10);
    CHECK_EQ(t.az_us, 1500);
    tracker_update(&t, bright_top, SKY_CLEAR, 20);
    CHECK_EQ(t.el_us, 1510);
    for (int i = 0; i < 300; i++) tracker_update(&t, bright_right, SKY_CLEAR, 30 + i * 10);
    CHECK_EQ(t.az_us, cfg.az_max_us);                           /* FR-04: не виходить за межі */
}

static void sign_inverts_direction(void)
{
    setup(); t.cfg.sign_az = -1; t.cfg.sign_el = -1;
    tracker_update(&t, bright_right, SKY_CLEAR, 0);
    CHECK_EQ(t.az_us, 1490);
}

static void zero_offset_is_subtracted(void)
{
    setup();
    tracker_update(&t, bright_right, SKY_CLEAR, 0);
    CHECK_EQ(t.ex, 500);                                   /* (4000-2000)/4000 */
    setup(); t.cfg.ex_off = 500;
    tracker_update(&t, bright_right, SKY_CLEAR, 0);
    CHECK_EQ(t.ex, 0); CHECK_EQ(t.az_us, 1500);            /* перекіс компенсовано: серво стоїть */
}

static void deadband_keeps_servo_still(void)
{
    setup();
    uint16_t q[4] = { 2000, 2030, 2000, 2030 };                 /* ex = 7 проміле < 30 */
    tracker_update(&t, q, SKY_CLEAR, 0);
    CHECK_EQ(t.az_us, 1500);
}

static void diffuse_light_is_overcast_and_servo_still(void)
{
    setup();
    uint16_t q[4] = { 2000, 2010, 2000, 2010 };                 /* розкид мал: розсіяне світло */
    tracker_update(&t, q, SKY_CLEAR, 0);
    CHECK_EQ(t.mode, MODE_OVERCAST);
    tracker_update(&t, bright_right, SKY_OVERCAST, 10);         /* FR-13: камера каже OVERCAST -> серво стоять */
    CHECK_EQ(t.mode, MODE_OVERCAST); CHECK_EQ(t.az_us, 1500);
    CHECK_EQ(tracker_update(&t, bright_right, SKY_CLEAR, 20), 1);   /* камера змінила думку -> TRACK */
    CHECK_EQ(t.mode, MODE_TRACK);
}

static void park_after_dark_time_and_back_with_hysteresis(void)
{
    setup(); t.az_us = 1700; t.el_us = 1200;
    tracker_update(&t, dark, SKY_DARK, 0);
    CHECK(t.mode != MODE_PARK);                                 /* поки темно менше за dark_time */
    tracker_update(&t, dark, SKY_DARK, 2999);
    CHECK(t.mode != MODE_PARK);
    CHECK_EQ(tracker_update(&t, dark, SKY_DARK, 3000), 1);
    CHECK_EQ(t.mode, MODE_PARK); CHECK_EQ(t.az_us, cfg.park_az_us); CHECK_EQ(t.el_us, cfg.park_el_us);
    uint16_t edge[4] = { 310, 310, 310, 310 };                  /* 1240: вище порога 1200, нижче порога з гістерезисом 1350 */
    tracker_update(&t, edge, SKY_OVERCAST, 3010);
    CHECK_EQ(t.mode, MODE_PARK);
    tracker_update(&t, bright_right, SKY_CLEAR, 3020);
    CHECK_EQ(t.mode, MODE_TRACK);
}

static void short_dark_pulse_does_not_park(void)
{
    setup();
    tracker_update(&t, dark, SKY_DARK, 0);
    tracker_update(&t, balanced, SKY_CLEAR, 1000);              /* світло повернулось: таймер скинуто */
    tracker_update(&t, dark, SKY_DARK, 1500);
    tracker_update(&t, dark, SKY_DARK, 4000);                   /* від 1500 пройшло лише 2500 мс */
    CHECK(t.mode != MODE_PARK);
}

static void spread_handles_all_zero(void)
{
    setup();
    uint16_t z[4] = { 0, 0, 0, 0 };
    tracker_update(&t, z, SKY_DARK, 0);
    CHECK_EQ(t.ex, 0);
}

static void mode_names(void)
{
    CHECK(strcmp(tracker_mode_name(MODE_TRACK), "TRACK") == 0);
    CHECK(strcmp(tracker_mode_name(MODE_OVERCAST), "OVERCAST") == 0);
    CHECK(strcmp(tracker_mode_name(MODE_PARK), "PARK") == 0);
}

/* ---- FR-14: втрата сигналу ---- */
static void loss_detects_halving_within_2s(void)
{
    loss_t l; loss_init(&l);
    int ev = 0;
    for (int i = 0; i < 20; i++) ev += loss_update(&l, 8000, i * 100);   /* стабільно */
    CHECK_EQ(ev, 0);
    CHECK_EQ(loss_update(&l, 3900, 2000), 1);                  /* стало менше половини від значення 2 с тому */
    CHECK_EQ(loss_update(&l, 3900, 2100), 0);                  /* повторна подія не раніше ніж через 5 с */
    int later = 0;
    for (int i = 0; i < 20; i++) later += loss_update(&l, 3900, 2200 + i * 100);
    CHECK_EQ(later, 0);
}

static void loss_ignores_slow_decay_and_zero_history(void)
{
    loss_t l; loss_init(&l);
    for (int i = 0; i < 20; i++) loss_update(&l, 0, i * 100);   /* історія нулів: ділення не потрібне, події немає */
    CHECK_EQ(loss_update(&l, 0, 2000), 0);
    loss_init(&l);
    int ev = 0;
    for (int i = 0; i < 60; i++) ev += loss_update(&l, 8000 - i * 20, i * 100);   /* повільне падіння: за 2 с -400 */
    CHECK_EQ(ev, 0);
}

static void loss_can_fire_again_after_5s(void)
{
    loss_t l; loss_init(&l);
    for (int i = 0; i < 20; i++) loss_update(&l, 8000, i * 100);
    CHECK_EQ(loss_update(&l, 1000, 2000), 1);
    for (int i = 0; i < 70; i++) loss_update(&l, 8000, 2100 + i * 100);          /* відновлення */
    CHECK_EQ(loss_update(&l, 1000, 9100), 1);                                   /* друге падіння, пройшло > 5 с */
}

void test_tracker(void)
{
    RUN(light_sum_and_polarity); RUN(track_moves_towards_light_and_clamps); RUN(sign_inverts_direction);
    RUN(deadband_keeps_servo_still); RUN(zero_offset_is_subtracted); RUN(diffuse_light_is_overcast_and_servo_still);
    RUN(park_after_dark_time_and_back_with_hysteresis); RUN(short_dark_pulse_does_not_park);
    RUN(spread_handles_all_zero); RUN(mode_names);
    RUN(loss_detects_halving_within_2s); RUN(loss_ignores_slow_decay_and_zero_history); RUN(loss_can_fire_again_after_5s);
}
