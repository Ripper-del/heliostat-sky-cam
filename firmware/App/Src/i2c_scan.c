/** @file i2c_scan.c — перебір усіх 7-бітних адрес на I2C1 (аналог i2cdetect). */
#include "main.h"
#include "selftest.h"

uint8_t i2c_scan(uint8_t *found, uint8_t max)
{
    uint8_t n = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        /* HAL очікує адресу, зсунуту на 1 біт вліво (R/W-біт займає молодший розряд). */
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1, 5) == HAL_OK) {
            if (n < max) found[n] = addr;
            n++;
        }
    }
    return n;
}
