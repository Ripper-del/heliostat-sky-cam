#include "ov7670.h"

void ov7670_init_dev(ov7670_t *d, const sccb_bus_t *bus) { d->bus = bus; }

int ov7670_identify(ov7670_t *d, uint8_t *pid, uint8_t *ver)
{
    int rc = DEV_ERR_BUS;
    for (int attempt = 0; attempt < 3; attempt++) {
        uint8_t p = 0, v = 0;
        rc = d->bus->read_reg(d->bus->ctx, OV7670_REG_PID, &p);
        if (rc == DEV_OK) rc = d->bus->read_reg(d->bus->ctx, OV7670_REG_VER, &v);
        if (rc == DEV_OK) {
            *pid = p; *ver = v;
            if (p == OV7670_PID_VALUE) return DEV_OK;
            rc = DEV_ERR_ID;      /* 0xFF/0x00 = порожня шина; інше значення = збій кадру: пробуємо ще */
        }
        if (d->bus->delay_ms) d->bus->delay_ms(5);
    }
    return rc;
}

int ov7670_soft_reset(ov7670_t *d)
{
    int rc = d->bus->write_reg(d->bus->ctx, OV7670_REG_COM7, 0x80);
    if (d->bus->delay_ms) d->bus->delay_ms(10);
    return rc;
}
