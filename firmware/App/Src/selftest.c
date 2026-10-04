/**
 * @file selftest.c
 * @brief Одноразова перевірка всіх підключених компонентів зі зведеною таблицею PASS/FAIL.
 *
 * Датчик, який не підключено, дає FAIL, але прошивка не зависає — це нормально для ЛР1.
 */
#include <stdio.h>
#include "main.h"
#include "selftest.h"

static void line(const char *name, int ok, const char *detail)
{
    printf("  %-22s %s  %s\r\n", name, ok ? "PASS" : "FAIL", detail);
}

void selftest_run_all(void)
{
    char buf[64];
    int pass = 0, total = 0;

    printf("\r\n--- SELFTEST ---\r\n");

    /* 1. I2C-скан */
    uint8_t found[16];
    uint8_t n = i2c_scan(found, 16);
    printf("  I2C scan: %u device(s):", (unsigned)n);
    for (uint8_t i = 0; i < n && i < 16; i++) printf(" 0x%02X", found[i]);
    printf("\r\n");

    /* 2. MPU-6050 */
    uint8_t who = 0;
    total++;
    if (mpu6050_probe(&who) == 0) {
        snprintf(buf, sizeof buf, "WHO_AM_I=0x%02X (expected 0x68)", who);
        line("MPU-6050 (I2C 0x68)", who == 0x68, buf);
        if (who == 0x68) pass++;
    } else {
        line("MPU-6050 (I2C 0x68)", 0, "no ACK on I2C");
    }

    /* 3. OV7670 */
    uint8_t pid = 0, ver = 0;
    total++;
    if (ov7670_probe(&pid, &ver) == 0) {
        snprintf(buf, sizeof buf, "PID=0x%02X VER=0x%02X (expected 76 73)", pid, ver);
        int ok = (pid == 0x76);
        line("OV7670 (SCCB 0x21)", ok, buf);
        if (ok) pass++;
    } else {
        line("OV7670 (SCCB 0x21)", 0, "no ACK (check XCLK on PA8, power, SDA/SCL)");
    }

    /* 4. SPI Flash */
    uint8_t id[3] = {0};
    total++;
    if (w25q_read_jedec(id) == 0) {
        snprintf(buf, sizeof buf, "JEDEC=%02X %02X %02X (expected EF 40 16/17)", id[0], id[1], id[2]);
        int ok = (id[0] == 0xEF);
        line("W25Q flash (SPI2)", ok, buf);
        if (ok) pass++;
    } else {
        line("W25Q flash (SPI2)", 0, "SPI error");
    }

    /* 5. ADC / квадрант фоторезисторів (TL, TR, BL, BR) */
    uint16_t q[4];
    quadrant_read_raw(q);
    static const char *labels[4] = { "TL (PA0)", "TR (PA1)", "BL (PA4)", "BR (PB0)" };
    for (int i = 0; i < 4; i++) {
        total++;
        snprintf(buf, sizeof buf, "raw=%u (%lu mV)", (unsigned)q[i], (unsigned long)photoresistor_raw_to_mv(q[i]));
        char name[24];
        snprintf(name, sizeof name, "GM5528 %s", labels[i]);
        int adc_ok = (q[i] > 20 && q[i] < 4075);   /* не "прилипло" до 0 або до максимуму */
        line(name, adc_ok, buf);
        if (adc_ok) pass++;
    }

    printf("--- RESULT: %d/%d PASS ---\r\n\r\n", pass, total);
}
