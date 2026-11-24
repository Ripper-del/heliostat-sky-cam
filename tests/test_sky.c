#include <string.h>
#include "test.h"
#include "sky.h"

static void fill(uint8_t *f, uint8_t v) { memset(f, v, FRAME_PIXELS); }

static void stats_uniform(void)
{
    static uint8_t f[FRAME_PIXELS]; fill(f, 100);
    frame_stats_t s = frame_stats(f, FRAME_PIXELS, 240);
    CHECK_EQ(s.mean, 100); CHECK_EQ(s.contrast, 0); CHECK_EQ(s.sat_permille, 0);
}

static void stats_two_levels(void)
{
    static uint8_t f[FRAME_PIXELS];                    /* половина 50, половина 250: mean 150, sigma 100 */
    for (size_t i = 0; i < FRAME_PIXELS; i++) f[i] = (i & 1) ? 250 : 50;
    frame_stats_t s = frame_stats(f, FRAME_PIXELS, 240);
    CHECK_EQ(s.mean, 150); CHECK_EQ(s.contrast, 100); CHECK_EQ(s.sat_permille, 500);
}

static void stats_empty(void)
{
    frame_stats_t s = frame_stats(NULL, 0, 240);
    CHECK_EQ(s.mean, 0); CHECK_EQ(s.contrast, 0); CHECK_EQ(s.sat_permille, 0);
}

static void stats_isqrt_small_values(void)
{
    uint8_t f[4] = {0, 0, 0, 3};                       /* mean 0, E[x^2] = 2 -> var 2 -> sigma 1 */
    frame_stats_t s = frame_stats(f, 4, 240);
    CHECK_EQ(s.mean, 0); CHECK_EQ(s.contrast, 1);
    uint8_t g[2] = {0, 1};                             /* ціле ділення: mean 0, E[x^2] 0 -> var 0 */
    s = frame_stats(g, 2, 240);
    CHECK_EQ(s.contrast, 0);
}

static void classify_three_states(void)
{
    static uint8_t f[FRAME_PIXELS];
    fill(f, 10);  frame_stats_t d = frame_stats(f, FRAME_PIXELS, 240);
    CHECK_EQ(sky_classify(&d, &SKY_DEFAULTS), SKY_DARK);
    fill(f, 120); frame_stats_t o = frame_stats(f, FRAME_PIXELS, 240);
    CHECK_EQ(sky_classify(&o, &SKY_DEFAULTS), SKY_OVERCAST);
    fill(f, 200); frame_stats_t b = frame_stats(f, FRAME_PIXELS, 240);   /* дуже яскравий кадр */
    CHECK_EQ(sky_classify(&b, &SKY_DEFAULTS), SKY_CLEAR);
}

static void classify_sun_spot_makes_clear(void)
{
    static uint8_t f[FRAME_PIXELS]; fill(f, 100);
    for (size_t i = 0; i < FRAME_PIXELS / 20; i++) f[i] = 255;   /* 5 % пересвіту при середній яскравості */
    frame_stats_t s = frame_stats(f, FRAME_PIXELS, 240);
    CHECK(s.mean < SKY_DEFAULTS.clear_mean);
    CHECK_EQ(sky_classify(&s, &SKY_DEFAULTS), SKY_CLEAR);
}

static void names_and_mismatch(void)
{
    CHECK(strcmp(sky_name(SKY_DARK), "DARK") == 0);
    CHECK(strcmp(sky_name(SKY_OVERCAST), "OVERCAST") == 0);
    CHECK(strcmp(sky_name(SKY_CLEAR), "CLEAR") == 0);
    CHECK_EQ(sky_mismatch(SKY_DARK, 1), 0);
    CHECK_EQ(sky_mismatch(SKY_CLEAR, 0), 0);
    CHECK_EQ(sky_mismatch(SKY_DARK, 0), 1);     /* камера закрита, фоторезистори освітлені */
    CHECK_EQ(sky_mismatch(SKY_OVERCAST, 1), 1); /* камера бачить світло, фоторезистори затемнені */
}

void test_sky(void)
{
    RUN(stats_uniform); RUN(stats_two_levels); RUN(stats_empty); RUN(stats_isqrt_small_values);
    RUN(classify_three_states); RUN(classify_sun_spot_makes_clear); RUN(names_and_mismatch);
}
