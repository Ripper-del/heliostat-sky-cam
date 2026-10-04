#ifndef SELFTEST_H
#define SELFTEST_H
#include <stdint.h>

/* --- i2c_scan.c --- */
uint8_t i2c_scan(uint8_t *found, uint8_t max);   /* повертає кількість знайдених 7-бітних адрес */

/* --- mpu6050_probe.c --- */
#define MPU6050_ADDR7      0x68                   /* AD0 = GND; при AD0 = VCC буде 0x69 */
#define MPU6050_WHO_AM_I   0x75                   /* регістр ідентифікації, очікуємо 0x68 */
int mpu6050_probe(uint8_t *who_am_i);             /* 0 = ОК, <0 = помилка шини */

/* --- ov7670_probe.c --- */
#define OV7670_ADDR7       0x21                   /* SCCB: 0x42 (write) / 0x43 (read) у 8-бітному записі */
int ov7670_probe(uint8_t *pid, uint8_t *ver);

/* --- w25q_probe.c --- */
int w25q_read_jedec(uint8_t id[3]);               /* команда 0x9F */

/* --- quadrant_photoresistors.c --- */
void quadrant_read_raw(uint16_t out[4]);          /* [TL, TR, BL, BR], кожен 0..4095 */
uint32_t photoresistor_raw_to_mv(uint16_t raw);

/* --- selftest.c --- */
void selftest_run_all(void);
#endif
