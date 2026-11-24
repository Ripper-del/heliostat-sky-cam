#include "test.h"
#include "quadrant.h"

static void raw_to_mv(void)
{
    CHECK_EQ(quadrant_raw_to_mv(0), 0);
    CHECK_EQ(quadrant_raw_to_mv(4095), 3300);
    CHECK_EQ(quadrant_raw_to_mv(2048), 1650);
    CHECK_EQ(quadrant_raw_to_mv(2554), 2058);        /* значення з UART-логу прошивки (ADC_TL) */
    CHECK_EQ(quadrant_raw_to_mv(3098), 2496);        /* ADC_TR */
}

static void error_balanced_and_directions(void)
{
    uint16_t eq[4] = { 2000, 2000, 2000, 2000 };
    quadrant_error_t e = quadrant_error(eq);
    CHECK_EQ(e.x_permille, 0); CHECK_EQ(e.y_permille, 0);

    uint16_t right[4] = { 1000, 3000, 1000, 3000 };  /* TR, BR яскравіші */
    e = quadrant_error(right);
    CHECK_EQ(e.x_permille, 500); CHECK_EQ(e.y_permille, 0);

    uint16_t top[4] = { 3000, 3000, 1000, 1000 };    /* TL, TR яскравіші */
    e = quadrant_error(top);
    CHECK_EQ(e.x_permille, 0); CHECK_EQ(e.y_permille, 500);

    uint16_t left_bottom[4] = { 1000, 0, 4000, 1000 };
    e = quadrant_error(left_bottom);
    CHECK(e.x_permille < 0); CHECK(e.y_permille < 0);
}

static void error_dark_and_extreme(void)
{
    uint16_t dark[4] = { 0, 0, 0, 0 };
    quadrant_error_t e = quadrant_error(dark);
    CHECK_EQ(e.x_permille, 0); CHECK_EQ(e.y_permille, 0);   /* ділення на 0 не відбувається */
    uint16_t one[4] = { 0, 4095, 0, 0 };
    e = quadrant_error(one);
    CHECK_EQ(e.x_permille, 1000); CHECK_EQ(e.y_permille, 1000);
}

static void servo_angle(void)
{
    CHECK_EQ(servo_angle_to_us(0, 500, 2500), 500);
    CHECK_EQ(servo_angle_to_us(180, 500, 2500), 2500);
    CHECK_EQ(servo_angle_to_us(90, 500, 2500), 1500);
    CHECK_EQ(servo_angle_to_us(90, 500, 2400), 1450);
    CHECK_EQ(servo_angle_to_us(-30, 500, 2500), 500);
    CHECK_EQ(servo_angle_to_us(999, 500, 2500), 2500);
}

static void servo_ramp(void)
{
    CHECK_EQ(servo_next_step(1500, 2000, 20), 1520);
    CHECK_EQ(servo_next_step(1990, 2000, 20), 2000);     /* без перельоту */
    CHECK_EQ(servo_next_step(1500, 1000, 20), 1480);
    CHECK_EQ(servo_next_step(1005, 1000, 20), 1000);
    CHECK_EQ(servo_next_step(1500, 1500, 20), 1500);
    uint32_t c = 1000; int steps = 0;                    /* 1000 -> 2000 за 50 кроків по 20 мкс */
    while (c != 2000) { c = servo_next_step(c, 2000, 20); steps++; }
    CHECK_EQ(steps, 50);
}

void test_quadrant(void)
{
    RUN(raw_to_mv); RUN(error_balanced_and_directions); RUN(error_dark_and_extreme); RUN(servo_angle); RUN(servo_ramp);
}
