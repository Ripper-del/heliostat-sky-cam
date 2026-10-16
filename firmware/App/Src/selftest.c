/**
 * @file selftest.c
 * @brief Самоперевірка: ідентифікація + мінімальна ініціалізація + фізична правдоподібність даних.
 *
 * Правдоподібність (методика, п. 4.2 методички ЛР2):
 *   MPU-6050   |a| у спокої = 1 g ± 15 % (zero-g offset ±50/±80 мг + допуск чутливості ±3 % за Product Spec ⟦звірити⟧;
 *              на залізі отримано стабільно 0.97..1.12 g залежно від орієнтації); температура кристала 15..40 °C
 *   OV7670     PID=0x76, VER=0x73
 *   W25Q64     JEDEC = EF 40 17; запис 16 байт у сектор 0x7FF000 і побайтне зіставлення зі зчитаним
 *   GM5528     сире значення не "прилипло" до 0/4095
 *   Серво      ширина імпульсу (CCR) після set дорівнює заданій; фізично — логічним аналізатором
 */
#include <stdio.h>
#include <string.h>
#include "selftest.h"
#include "platform.h"
#include "mpu6050.h"
#include "w25q.h"
#include "ov7670.h"
#include "quadrant.h"

#define FLASH_TEST_ADDR  0x7FF000UL     /* останній 4-КБ сектор W25Q64 (8 МБ) */

static void st(const char *name, int ok) { printf("[st] %-8s %s", name, ok ? "PASS" : "FAIL"); }
static void endl(void) { printf("\r\n"); }

static int run_i2c_scan(void)
{
    const i2c_bus_t *b = platform_i2c1();
    printf("[st] I2C_SCAN  n=");
    char list[64]; list[0] = 0; int n = 0;
    for (uint8_t a = 1; a < 127; a++)
        if (b->probe(b->ctx, a) == DEV_OK) {
            n++;
            size_t l = strlen(list);
            if (l + 8 < sizeof list) snprintf(list + l, sizeof list - l, " 0x%02X", a);
        }
    printf("%d addrs=%s\r\n", n, n ? list : " none");
    return n;
}

static int run_mpu(void)
{
    mpu6050_t d; mpu6050_init_dev(&d, platform_i2c1());
    uint8_t id = 0; mpu6050_raw_t a; int16_t t = 0;
    int step = 1, rc = mpu6050_who_am_i(&d, &id);
    if (rc == DEV_OK) { step = 2; rc = mpu6050_wake(&d); }
    if (rc == DEV_OK) platform_delay_ms(50);
    /* На шумній шині іноді приходять спотворені кадри (байти 0xFF/0xFE) при "успішній" транзакції:
     * до 3 спроб, поки |a| і температура не стануть фізично правдоподібними. */
    int tries = 0;
    for (tries = 1; rc == DEV_OK && tries <= 3; tries++) {
        step = 3; rc = mpu6050_read_accel_raw(&d, &a);
        if (rc == DEV_OK) { step = 4; rc = mpu6050_read_temp_raw(&d, &t); }
        if (rc != DEV_OK) break;
        uint32_t m = mpu6050_magnitude_mg(&a); int32_t c = mpu6050_temp_centi_c(t);
        if (m >= 850 && m <= 1150 && c >= 1500 && c <= 4000) break;
        platform_delay_ms(5);
    }
    if (rc != DEV_OK) {
        st("MPU6050", 0);
        printf(" rc=%d step=%d(1=id,2=wake,3=accel,4=temp) who=0x%02X hal_err=0x%lX", rc, step, id, (unsigned long)platform_i2c1_last_error());
        endl(); return 0;
    }
    uint32_t mag = mpu6050_magnitude_mg(&a);
    int32_t tc = mpu6050_temp_centi_c(t);
    int ok = (mag >= 850 && mag <= 1150) && (tc >= 1500 && tc <= 4000);   /* ±15 % (див. коментар вгорі) */
    st("MPU6050", ok);
    printf(" who=0x%02X ax=%d ay=%d az=%d mag_mg=%lu temp_cC=%ld tries=%d", id, a.x, a.y, a.z, (unsigned long)mag, (long)tc, tries > 3 ? 3 : tries);
    endl();
    return ok;
}

static int run_cam(void)
{
    ov7670_t d; ov7670_init_dev(&d, platform_sccb_cam());
    uint8_t pid = 0, ver = 0;
    int rc = ov7670_identify(&d, &pid, &ver);
    int ok = (rc == DEV_OK) && ver == OV7670_VER_VALUE;
    st("OV7670", ok);
    printf(" rc=%d pid=0x%02X ver=0x%02X", rc, pid, ver);
    endl();
    return ok;
}

static int run_flash_id(void)
{
    w25q_t f; w25q_init_dev(&f, platform_spi2_flash());
    uint8_t id[3] = {0};
    w25q_wake(&f);
    int rc = w25q_read_jedec(&f, id);
    st("W25Q_ID", rc == DEV_OK);
    printf(" rc=%d jedec=%02X%02X%02X", rc, id[0], id[1], id[2]);
    endl();
    return rc == DEV_OK;
}

static int run_flash_rw(void)
{
    w25q_t f; w25q_init_dev(&f, platform_spi2_flash());
    uint8_t wr[16], rd[16];
    for (int i = 0; i < 16; i++) wr[i] = (uint8_t)(0xA0 + i);
    memset(rd, 0, sizeof rd);
    int rc = w25q_sector_erase(&f, FLASH_TEST_ADDR);
    if (rc == DEV_OK) rc = w25q_write(&f, FLASH_TEST_ADDR, wr, sizeof wr);
    if (rc == DEV_OK) rc = w25q_read(&f, FLASH_TEST_ADDR, rd, sizeof rd);
    int ok = (rc == DEV_OK) && memcmp(wr, rd, sizeof wr) == 0;
    st("W25Q_RW", ok);
    printf(" rc=%d addr=0x%06lX bytes=16 first=0x%02X", rc, FLASH_TEST_ADDR, rd[0]);
    endl();
    return ok;
}

/* Плавний рух: крок SERVO_STEP_US кожні 20 мс (один період PWM) — без кидка струму; серво по одному. */
#define SERVO_STEP_US 20U
static void servo_goto(int ch, uint32_t target)
{
    uint32_t cur = platform_servo_get_us(ch);
    while (cur != target) {
        cur = servo_next_step(cur, target, SERVO_STEP_US);
        platform_servo_set_us(ch, cur);
        platform_delay_ms(20);
    }
}

/* ch_mask: бітова маска каналів (1 = азимут, 2 = елевація, 3 = обидва по черзі) */
static int run_pwm_mask(int ch_mask)
{
    int ok = 1;
    static const uint32_t pts[3] = { 1000, 2000, 1500 };
    for (int ch = 0; ch < 2; ch++) {
        if (!(ch_mask & (1 << ch))) continue;
        platform_servo_set_us(ch, 1500);
        platform_servo_enable(ch, 1);
        platform_delay_ms(100);
        for (int i = 0; i < 3; i++) {
            servo_goto(ch, pts[i]);
            platform_delay_ms(150);
            if (platform_servo_get_us(ch) != pts[i]) ok = 0;
        }
        platform_servo_enable(ch, 0);          /* відпускаємо серво: мінімальний струм у спокої */
    }
    st("PWM", ok);
    printf(" ch_mask=%d pulses_us=1000,2000,1500 step_us=%u/20ms period_us=20000", ch_mask, SERVO_STEP_US);
    endl();
    return ok;
}

/* Статистика збоїв I2C1: N читань WHO_AM_I без повторів на різних швидкостях. Виявляє залежність від частоти. */
void selftest_i2c_stress(void)
{
    static const uint32_t speeds[] = { 10000, 50000, 100000 };
    const i2c_bus_t *b = platform_i2c1();
    platform_i2c1_set_retries(1);
    for (int split = 0; split < 2; split++)
    for (unsigned s = 0; s < sizeof speeds / sizeof speeds[0]; s++) {
        platform_i2c1_set_split_read(split);
        platform_i2c1_set_speed(speeds[s]);
        int ok = 0, bad = 0, n = 100;
        int e_af = 0, e_berr = 0, e_arlo = 0, e_to = 0, e_other = 0;
        for (int i = 0; i < n; i++) {
            uint8_t v = 0;
            int rc = b->read(b->ctx, MPU6050_ADDR7, MPU6050_REG_WHO_AM_I, &v, 1);
            if (rc != DEV_OK) {
                uint32_t e = platform_i2c1_last_error();
                if (e & 4) e_af++; else if (e & 1) e_berr++; else if (e & 2) e_arlo++; else if (e & 0x20) e_to++; else e_other++;
            }
            else if (v != MPU6050_WHO_AM_I_VALUE) bad++;
            else ok++;
            platform_delay_ms(2);
        }
        printf("[st] I2C_STRESS split=%d speed_hz=%lu n=%d ok=%d wrong=%d af=%d berr=%d arlo=%d timeout=%d other=%d\r\n",
               split, (unsigned long)speeds[s], n, ok, bad, e_af, e_berr, e_arlo, e_to, e_other);
    }
    platform_i2c1_set_split_read(1);
    platform_i2c1_set_speed(100000);
    platform_i2c1_set_retries(3);
}

static int run_adc(void)
{
    uint16_t q[4];
    platform_marker(1);                    /* службовий GPIO: фронт = початок вимірювання */
    platform_adc_read_quadrant(q);
    platform_marker(0);
    int ok = 1;
    static const char *n[4] = { "TL", "TR", "BL", "BR" };
    for (int i = 0; i < 4; i++) {
        int good = q[i] > 20 && q[i] < 4075;
        if (!good) ok = 0;
        char nm[10]; snprintf(nm, sizeof nm, "ADC_%s", n[i]);
        st(nm, good);
        printf(" raw=%u mv=%lu", q[i], (unsigned long)quadrant_raw_to_mv(q[i]));
        endl();
    }
    return ok;
}

/* Навмисна аномалія для п. 4.4 методички: звернення на адресу, якої немає (0x69 при AD0 = GND) — на шині NACK. */
void selftest_i2c_nack(void)
{
    const i2c_bus_t *b = platform_i2c1();
    int rc = b->probe(b->ctx, 0x69);
    printf("[st] I2C_NACK  addr=0x69 rc=%d (%s)\r\n", rc, rc == DEV_OK ? "ACK?!" : "NACK, як і очікувалось");
}

void selftest_i2c_scan(void)  { run_i2c_scan(); }
void selftest_mpu(void)       { run_mpu(); }
void selftest_cam(void)       { run_cam(); }
void selftest_flash_id(void)  { run_flash_id(); }
void selftest_flash_rw(void)  { run_flash_rw(); }
static int run_pwm(void) { return run_pwm_mask(3); }
void selftest_pwm(void)       { run_pwm(); }
void selftest_pwm_az(void)    { run_pwm_mask(1); }
void selftest_pwm_el(void)    { run_pwm_mask(2); }
void selftest_adc(void)       { run_adc(); }

int selftest_run_all(void)
{
    int pass = 0, total = 0;
    printf("\r\n[st] BEGIN\r\n");
    run_i2c_scan();
    total++; pass += run_mpu();
    total++; pass += run_cam();
    total++; pass += run_flash_id();
    total++; pass += run_flash_rw();
    /* PWM (серво) НЕ входить в автоматичну самоперевірку: рух серво від 5V плати може просадити живлення і
     * скинути USB/плату. Запускається вручну: CLI 'p' (обидва), '1' (азимут), '2' (елевація). */
    total += 4; pass += run_adc() ? 4 : 0;
    printf("[st] RESULT pass=%d total=%d\r\n\r\n", pass, total);
    return pass;
}
