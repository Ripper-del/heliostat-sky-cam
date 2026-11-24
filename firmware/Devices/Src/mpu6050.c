#include "mpu6050.h"

void mpu6050_init_dev(mpu6050_t *d, const i2c_bus_t *bus) { d->bus = bus; }

int mpu6050_who_am_i(mpu6050_t *d, uint8_t *id)
{
    int rc = DEV_ERR_BUS;
    for (int attempt = 0; attempt < 3; attempt++) {   /* ACK є, але дані можуть бути спотворені наводками на SDA */
        uint8_t v = 0;
        if (d->bus->read(d->bus->ctx, MPU6050_ADDR7, MPU6050_REG_WHO_AM_I, &v, 1) != DEV_OK) { rc = DEV_ERR_BUS; continue; }
        *id = v;
        if (v == MPU6050_WHO_AM_I_VALUE) return DEV_OK;
        rc = DEV_ERR_ID;
    }
    return rc;
}

int mpu6050_wake(mpu6050_t *d)
{
    uint8_t zero = 0;
    return d->bus->write(d->bus->ctx, MPU6050_ADDR7, MPU6050_REG_PWR_MGMT_1, &zero, 1) == DEV_OK
           ? DEV_OK : DEV_ERR_BUS;
}

static int16_t be16(const uint8_t *p) { return (int16_t)(((uint16_t)p[0] << 8) | p[1]); }

int mpu6050_read_accel_raw(mpu6050_t *d, mpu6050_raw_t *out)
{
    uint8_t b[6];
    if (d->bus->read(d->bus->ctx, MPU6050_ADDR7, MPU6050_REG_ACCEL_XOUT_H, b, 6) != DEV_OK)
        return DEV_ERR_BUS;
    out->x = be16(b); out->y = be16(b + 2); out->z = be16(b + 4);
    return DEV_OK;
}

int mpu6050_read_temp_raw(mpu6050_t *d, int16_t *out)
{
    uint8_t b[2];
    if (d->bus->read(d->bus->ctx, MPU6050_ADDR7, MPU6050_REG_TEMP_OUT_H, b, 2) != DEV_OK)
        return DEV_ERR_BUS;
    *out = be16(b);
    return DEV_OK;
}

int32_t mpu6050_raw_to_mg(int16_t raw)
{
    return ((int32_t)raw * 1000) / MPU6050_LSB_PER_G_2G;
}

int32_t mpu6050_temp_centi_c(int16_t raw)
{
    return ((int32_t)raw * 100) / 340 + 3653;
}

static uint32_t isqrt64(uint64_t n)
{
    uint64_t r = 0, bit = 1ULL << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}

uint32_t mpu6050_magnitude_mg(const mpu6050_raw_t *raw)
{
    int64_t x = mpu6050_raw_to_mg(raw->x), y = mpu6050_raw_to_mg(raw->y), z = mpu6050_raw_to_mg(raw->z);
    return isqrt64((uint64_t)(x * x + y * y + z * z));
}
