/**
 * @file ov7670_probe.c
 * @brief Перевірка камери OV7670 через SCCB (I2C-подібна шина конфігурації).
 *
 * Важливо:
 *  - Камера відповідає лише при наявності тактового сигналу XCLK (10..48 МГц).
 *    Ми подаємо 16 МГц з MCO1 (PA8), див. main.c -> MX_MCO1_Init().
 *  - SCCB не підтримує repeated-start, тому читання робимо у два кроки:
 *    (1) записати адресу регістра + STOP, (2) прочитати байт.
 *  - Регістри ідентифікації: PID (0x0A) = 0x76, VER (0x0B) = 0x73.
 */
#include "main.h"
#include "selftest.h"

static int sccb_read(uint8_t reg, uint8_t *val)
{
    if (HAL_I2C_Master_Transmit(&hi2c1, OV7670_ADDR7 << 1, &reg, 1, 50) != HAL_OK) return -1;
    if (HAL_I2C_Master_Receive(&hi2c1, OV7670_ADDR7 << 1, val, 1, 50) != HAL_OK)   return -2;
    return 0;
}

int ov7670_probe(uint8_t *pid, uint8_t *ver)
{
    int r = sccb_read(0x0A, pid);
    if (r) return r;
    return sccb_read(0x0B, ver);
}
