/**
 * @file mpu6050_probe.c
 * @brief Перевірка присутності MPU-6050 (GY-521) за регістром WHO_AM_I.
 *
 * Це ще не драйвер (драйвер — завдання ЛР2), лише доказ, що I2C-шина жива
 * і датчик відповідає за адресою 0x68.
 */
#include "main.h"
#include "selftest.h"

int mpu6050_probe(uint8_t *who_am_i)
{
    uint8_t v = 0;
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDR7 << 1,
                                            MPU6050_WHO_AM_I, I2C_MEMADD_SIZE_8BIT,
                                            &v, 1, 50);
    if (st != HAL_OK) return -1;
    *who_am_i = v;
    return 0;
}
