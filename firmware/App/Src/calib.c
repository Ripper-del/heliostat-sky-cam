#include "calib.h"
#include "crc16.h"


static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int calib_check_range(uint16_t lo, uint16_t hi)
{
    return (lo >= 400 && hi <= 2600 && lo < hi) ? 0 : -1;
}

int calib_plan_save(fjob_t *j, const tracker_cfg_t *c, uint8_t b[CALIB_LEN])
{
    for (unsigned i = 0; i < CALIB_LEN; i++) b[i] = 0;
    put32(b, CALIB_MAGIC);
    put16(b + 4, c->az_min_us); put16(b + 6, c->az_max_us);
    put16(b + 8, c->el_min_us); put16(b + 10, c->el_max_us);
    put16(b + 12, c->park_az_us); put16(b + 14, c->park_el_us);
    put32(b + 16, c->dark_light_thr);
    b[20] = (uint8_t)c->invert_light; b[21] = (uint8_t)c->sign_az; b[22] = (uint8_t)c->sign_el;
    put16(b + 24, (uint16_t)c->ex_off); put16(b + 26, (uint16_t)c->ey_off);
    put16(b + 30, crc16(b, 30));
    fjob_init(j);
    fjob_erase(j, CALIB_ADDR);
    fjob_prog(j, CALIB_ADDR, b, CALIB_LEN);
    return DEV_OK;
}

int calib_save(w25q_t *f, const tracker_cfg_t *c)
{
    fjob_t j; uint8_t b[CALIB_LEN];
    calib_plan_save(&j, c, b);
    return fjob_run_blocking(&j, f);
}

int calib_load(w25q_t *f, tracker_cfg_t *c)
{
    uint8_t b[CALIB_LEN];
    int rc = w25q_read(f, CALIB_ADDR, b, CALIB_LEN);
    if (rc != DEV_OK) return rc;
    if (get32(b) != CALIB_MAGIC || get16(b + 30) != crc16(b, 30)) return DEV_ERR_ID;
    c->az_min_us = get16(b + 4); c->az_max_us = get16(b + 6);
    c->el_min_us = get16(b + 8); c->el_max_us = get16(b + 10);
    c->park_az_us = get16(b + 12); c->park_el_us = get16(b + 14);
    c->dark_light_thr = get32(b + 16);
    c->invert_light = (int8_t)b[20]; c->sign_az = (int8_t)b[21]; c->sign_el = (int8_t)b[22];
    c->ex_off = (int16_t)get16(b + 24); c->ey_off = (int16_t)get16(b + 26);
    return DEV_OK;
}
