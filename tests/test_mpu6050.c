#include "test.h"
#include "mpu6050.h"
#include "fake_i2c.h"
#include "vectors.h"

static fake_i2c_t f; static mpu6050_t d;
static void setup(void) { fake_i2c_init(&f); mpu6050_init_dev(&d, &f.bus); }

static void who_am_i_ok(void)
{
    setup(); f.regs[MPU6050_REG_WHO_AM_I] = VEC_MPU_WHO_AM_I;
    uint8_t id = 0;
    CHECK_EQ(mpu6050_who_am_i(&d, &id), DEV_OK);
    CHECK_EQ(id, 0x68);
    CHECK_EQ(f.log[0].addr, 0x68);
}

static void who_am_i_nack(void)
{
    setup(); f.nack_all = 1;
    uint8_t id = 0xAA;
    CHECK_EQ(mpu6050_who_am_i(&d, &id), DEV_ERR_BUS);
    CHECK_EQ(id, 0xAA);                       /* при помилці вихідний параметр не змінюється */
}

static void who_am_i_unexpected_id(void)
{
    setup(); f.regs[MPU6050_REG_WHO_AM_I] = 0x70;   /* напр., інший чип на 0x68 */
    uint8_t id = 0;
    CHECK_EQ(mpu6050_who_am_i(&d, &id), DEV_ERR_ID);
    CHECK_EQ(id, 0x70);
}

static void who_am_i_retries_on_corrupted_read(void)
{
    setup(); f.regs[MPU6050_REG_WHO_AM_I] = 0xE3;     /* спотворене читання */
    uint8_t id = 0;
    CHECK_EQ(mpu6050_who_am_i(&d, &id), DEV_ERR_ID);
    CHECK_EQ(f.nlog, 3);                              /* рівно 3 спроби */
    setup(); f.regs[MPU6050_REG_WHO_AM_I] = 0x68; f.nack_count = 2;   /* два NACK, третя спроба успішна */
    CHECK_EQ(mpu6050_who_am_i(&d, &id), DEV_OK);
    CHECK_EQ(f.nlog, 3);
}

static void wake_writes_pwr_mgmt(void)
{
    setup(); f.regs[MPU6050_REG_PWR_MGMT_1] = 0x40;   /* sleep=1 після скидання */
    CHECK_EQ(mpu6050_wake(&d), DEV_OK);
    CHECK_EQ(f.regs[MPU6050_REG_PWR_MGMT_1], 0);
    CHECK_EQ(f.nlog, 1); CHECK(f.log[0].is_write); CHECK_EQ(f.log[0].reg, 0x6B);
    f.nack_all = 1;
    CHECK_EQ(mpu6050_wake(&d), DEV_ERR_BUS);
}

static void accel_is_one_burst_big_endian(void)
{
    setup();
    memcpy(&f.regs[0x3B], VEC_MPU_ACCEL_BURST, 6);
    mpu6050_raw_t a;
    CHECK_EQ(mpu6050_read_accel_raw(&d, &a), DEV_OK);
    CHECK_EQ(f.nlog, 1);                       /* одне багатобайтове читання, а не 6 окремих */
    CHECK_EQ(f.log[0].reg, 0x3B); CHECK_EQ(f.log[0].len, 6);
    CHECK_EQ(a.x, 7708); CHECK_EQ(a.y, -12096); /* 0xD0C0 = -12096 (знаковий big-endian) */
    CHECK_EQ(a.z, -7512);
    /* ті самі байти => ті самі мілі-g, що виводить прошивка */
    CHECK_EQ(mpu6050_raw_to_mg(a.x), 470);     /* 7708*1000/16384 = 470.4 */
    CHECK_EQ(mpu6050_raw_to_mg(a.z), -458);    /* -7512*1000/16384 = -458.5 */
    CHECK(mpu6050_magnitude_mg(&a) > 950 && mpu6050_magnitude_mg(&a) < 1050); /* |a| ≈ 1 g */
    f.nack_all = 1;
    CHECK_EQ(mpu6050_read_accel_raw(&d, &a), DEV_ERR_BUS);
}

static void conversions(void)
{
    CHECK_EQ(mpu6050_raw_to_mg(16384), 1000);
    CHECK_EQ(mpu6050_raw_to_mg(-16384), -1000);
    CHECK_EQ(mpu6050_raw_to_mg(0), 0);
    CHECK_EQ(mpu6050_raw_to_mg(8192), 500);
    /* datasheet: T = raw/340 + 36.53 °C */
    CHECK_EQ(mpu6050_temp_centi_c(0), 3653);
    CHECK_EQ(mpu6050_temp_centi_c(3400), 4653);
    CHECK_EQ(mpu6050_temp_centi_c(-3400), 2653);
}

static void magnitude(void)
{
    mpu6050_raw_t rest = { 0, 0, 16384 };
    CHECK_EQ(mpu6050_magnitude_mg(&rest), 1000);          /* 1 g у спокої */
    mpu6050_raw_t tilt = { 11585, 0, 11585 };             /* нахил 45°: |a| все ще ≈ 1 g */
    uint32_t m = mpu6050_magnitude_mg(&tilt);
    CHECK(m >= 998 && m <= 1002);
    mpu6050_raw_t zero = { 0, 0, 0 };
    CHECK_EQ(mpu6050_magnitude_mg(&zero), 0);
}

static void temp_read(void)
{
    setup(); f.regs[0x41] = 0x00; f.regs[0x42] = 0x00;
    int16_t t = 123;
    CHECK_EQ(mpu6050_read_temp_raw(&d, &t), DEV_OK); CHECK_EQ(t, 0);
    f.regs[0x41] = 0xF2; f.regs[0x42] = 0x00;           /* -3584 */
    CHECK_EQ(mpu6050_read_temp_raw(&d, &t), DEV_OK); CHECK_EQ(t, -3584);
    f.nack_all = 1;
    CHECK_EQ(mpu6050_read_temp_raw(&d, &t), DEV_ERR_BUS);
}

void test_mpu6050(void)
{
    RUN(who_am_i_ok); RUN(who_am_i_nack); RUN(who_am_i_unexpected_id); RUN(who_am_i_retries_on_corrupted_read); RUN(wake_writes_pwr_mgmt);
    RUN(accel_is_one_burst_big_endian); RUN(conversions); RUN(magnitude); RUN(temp_read);
}
