/* Тестові вектори. Джерело — вивід UART прошивки на реальному залізі (самоперевірка, 2026-10-16)
 * та байти, зафіксовані логічним аналізатором (docs/captures/i2c_mpu6050_whoami_burst.sr, 2026-10-19). */
#ifndef VECTORS_H
#define VECTORS_H
/* W25Q64JV: команда 0x9F -> EF 40 17 (UART: "W25Q_ID PASS jedec=EF4017") */
static const unsigned char VEC_JEDEC[3]    = { 0xEF, 0x40, 0x17 };
/* OV7670: PID(0x0A)=0x76, VER(0x0B)=0x73 (UART: "OV7670 PASS pid=0x76 ver=0x73") */
static const unsigned char VEC_OV7670_ID[2] = { 0x76, 0x73 };
/* MPU-6050 WHO_AM_I = 0x68 (UART ЛР1: "WHO_AM_I=0x68, PASS") */
#define VEC_MPU_WHO_AM_I 0x68
/* Burst-читання 6 байт (ACCEL_XOUT_H..ZOUT_L), байти з реального запису .sr (I²C-декодер, адреса 0x68, рег. 0x3B).
 * ax=7708 ay=-12096 az=-7512 (сирі), |a| ≈ 990 мг. */
static const unsigned char VEC_MPU_ACCEL_BURST[6] = { 0x1E, 0x1C, 0xD0, 0xC0, 0xE2, 0xA8 };
#endif
