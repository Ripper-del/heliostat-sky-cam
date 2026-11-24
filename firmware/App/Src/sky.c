#include "sky.h"

const sky_thresholds_t SKY_DEFAULTS = { 40, 240, 20, 170 };

static uint32_t isqrt(uint32_t v)
{
    uint32_t r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

frame_stats_t frame_stats(const uint8_t *y, size_t n, uint8_t sat_level)
{
    frame_stats_t s = {0, 0, 0};
    if (n == 0) return s;
    uint64_t sum = 0, sq = 0; uint32_t sat = 0;
    for (size_t i = 0; i < n; i++) {
        sum += y[i]; sq += (uint32_t)y[i] * y[i];
        if (y[i] >= sat_level) sat++;
    }
    uint32_t mean = (uint32_t)(sum / n);
    uint64_t ex2 = sq / n;
    uint32_t var = ex2 > (uint64_t)mean * mean ? (uint32_t)(ex2 - (uint64_t)mean * mean) : 0;
    s.mean = (uint8_t)mean;
    s.contrast = (uint8_t)isqrt(var);
    s.sat_permille = (uint16_t)((uint64_t)sat * 1000 / n);
    return s;
}

sky_t sky_classify(const frame_stats_t *s, const sky_thresholds_t *t)
{
    if (s->mean < t->dark_mean) return SKY_DARK;
    if (s->sat_permille >= t->clear_sat_permille || s->mean >= t->clear_mean) return SKY_CLEAR;
    return SKY_OVERCAST;
}

const char *sky_name(sky_t s)
{
    return s == SKY_DARK ? "DARK" : s == SKY_CLEAR ? "CLEAR" : "OVERCAST";
}

int sky_mismatch(sky_t cam, int ldr_dark)
{
    return (cam == SKY_DARK) != (ldr_dark != 0);
}
