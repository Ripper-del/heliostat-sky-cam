#include <string.h>
#include "fake_i2c.h"

static int fail_now(fake_i2c_t *f)
{
    if (f->nack_all) return 1;
    if (f->nack_count > 0) { f->nack_count--; return 1; }
    return 0;
}

static int f_read(void *ctx, uint8_t a, uint8_t reg, uint8_t *buf, uint16_t len)
{
    fake_i2c_t *f = ctx;
    if (f->nlog < 32) f->log[f->nlog++] = (i2c_log_t){ 0, a, reg, len };
    if (fail_now(f)) return DEV_ERR_BUS;
    for (uint16_t i = 0; i < len; i++) buf[i] = f->regs[(uint8_t)(reg + i)];
    return DEV_OK;
}

static int f_write(void *ctx, uint8_t a, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    fake_i2c_t *f = ctx;
    if (f->nlog < 32) f->log[f->nlog++] = (i2c_log_t){ 1, a, reg, len };
    if (fail_now(f)) return DEV_ERR_BUS;
    for (uint16_t i = 0; i < len; i++) f->regs[(uint8_t)(reg + i)] = buf[i];
    return DEV_OK;
}

static int f_probe(void *ctx, uint8_t a) { (void)ctx; (void)a; return DEV_OK; }

void fake_i2c_init(fake_i2c_t *f)
{
    memset(f, 0, sizeof *f);
    f->bus = (i2c_bus_t){ f, f_read, f_write, f_probe };
}
