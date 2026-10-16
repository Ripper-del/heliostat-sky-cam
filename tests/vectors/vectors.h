/* Тестові вектори. Джерело кожного — вивід UART прошивки на реальному залізі (самоперевірка, 2026-10-16).
 * Байти, зафіксовані логічним аналізатором (docs/captures/), додаються сюди після запису в PulseView:
 * потрібно підставити decoded-байти MPU-6050 burst-читання (ACCEL_XOUT_H..) у VEC_MPU_ACCEL_BURST. */
#ifndef VECTORS_H
#define VECTORS_H
/* W25Q64JV: команда 0x9F -> EF 40 17 (UART: "W25Q_ID PASS jedec=EF4017") */
static const unsigned char VEC_JEDEC[3]    = { 0xEF, 0x40, 0x17 };
/* OV7670: PID(0x0A)=0x76, VER(0x0B)=0x73 (UART: "OV7670 PASS pid=0x76 ver=0x73") */
static const unsigned char VEC_OV7670_ID[2] = { 0x76, 0x73 };
/* MPU-6050 WHO_AM_I = 0x68 (UART ЛР1: "WHO_AM_I=0x68, PASS") */
#define VEC_MPU_WHO_AM_I 0x68
/* Burst-читання 6 байт у спокої, плата лежить горизонтально (az ≈ +1 g).
 * ПРИКЛАДНІ значення — ЗАМІНИТИ на байти з запису .sr; очікування (мг) рахуються з них у тесті. */
static const unsigned char VEC_MPU_ACCEL_BURST[6] = { 0x00, 0x40, 0xFF, 0xE0, 0x3F, 0xE0 };
#endif
