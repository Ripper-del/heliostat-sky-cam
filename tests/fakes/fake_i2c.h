/* FAKE I²C: пам'ять регістрів + інʼєкція NACK + журнал транзакцій (для перевірки послідовності). */
#ifndef FAKE_I2C_H
#define FAKE_I2C_H
#include "bus.h"

typedef struct { int is_write; uint8_t addr, reg; uint16_t len; } i2c_log_t;

typedef struct {
    uint8_t regs[256];
    int nack_all;            /* усі обміни -> DEV_ERR_BUS */
    int nack_count;          /* перші N обмінів -> DEV_ERR_BUS */
    i2c_log_t log[32];
    int nlog;
    i2c_bus_t bus;
} fake_i2c_t;

void fake_i2c_init(fake_i2c_t *f);
#endif
