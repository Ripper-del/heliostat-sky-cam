/** @file mpu6050.h — драйвер MPU-6050 (InvenSense, Register Map RM-MPU-6000A). Залежить лише від bus.h. */
#ifndef MPU6050_H
#define MPU6050_H
#include "bus.h"

#define MPU6050_ADDR7        0x68   /* AD0 = GND; при AD0 = VCC буде 0x69 */
#define MPU6050_REG_PWR_MGMT_1  0x6B
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_TEMP_OUT_H   0x41
#define MPU6050_REG_WHO_AM_I     0x75
#define MPU6050_WHO_AM_I_VALUE   0x68
#define MPU6050_LSB_PER_G_2G     16384   /* чутливість при діапазоні ±2 g */

typedef struct { int16_t x, y, z; } mpu6050_raw_t;

typedef struct { const i2c_bus_t *bus; } mpu6050_t;

void mpu6050_init_dev(mpu6050_t *d, const i2c_bus_t *bus);
/** Читає WHO_AM_I; DEV_ERR_ID, якщо не 0x68. */
int  mpu6050_who_am_i(mpu6050_t *d, uint8_t *id);
/** Виводить із sleep (PWR_MGMT_1 = 0), діапазон за замовчуванням ±2 g. */
int  mpu6050_wake(mpu6050_t *d);
/** Одне багатобайтове читання 6 байт ACCEL_XOUT_H..ACCEL_ZOUT_L (big-endian). */
int  mpu6050_read_accel_raw(mpu6050_t *d, mpu6050_raw_t *out);
int  mpu6050_read_temp_raw(mpu6050_t *d, int16_t *out);

/** Чисті функції (без шини), перевіряються тестами. */
int32_t  mpu6050_raw_to_mg(int16_t raw);                     /* ±2 g: raw*1000/16384 */
int32_t  mpu6050_temp_centi_c(int16_t raw);                  /* datasheet: T = raw/340 + 36.53 °C */
uint32_t mpu6050_magnitude_mg(const mpu6050_raw_t *raw);     /* |a| у мілі-g */
#endif
