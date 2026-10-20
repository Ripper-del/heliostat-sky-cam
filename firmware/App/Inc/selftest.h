#ifndef SELFTEST_H
#define SELFTEST_H
/** Самоперевірка всіх пристроїв. Формат рядків (для автоматичного розбору):
 *    [st] <NAME> <PASS|FAIL> key=value key=value ...
 *    [st] RESULT pass=<n> total=<m>
 *  Повертає кількість пройдених перевірок. */
int selftest_run_all(void);
/** Окремі транзакції (для запису логічним аналізатором, викликаються з CLI). */
void selftest_i2c_scan(void);
void selftest_mpu(void);
void selftest_cam(void);
void selftest_flash_id(void);
void selftest_flash_rw(void);
void selftest_pwm(void);
void selftest_pwm_az(void);
void selftest_pwm_el(void);
void selftest_adc(void);
void selftest_i2c_stress(void);
void selftest_mpu_stream(void);   /* ~60 с I²C-трафіку для запису аналізатором */
void selftest_i2c_nack(void);     /* навмисний NACK (адреса 0x69) для запису аномалії */
#endif
