/**
 * @file bus.h
 * @brief Інтерфейси шин, через які драйвери пристроїв працюють із залізом.
 *
 * Драйвер (Devices/) знає ТІЛЬКИ цей файл: жодного HAL. На платі інтерфейс реалізує
 * Platform/ (поверх HAL або bit-bang), у хостовому тесті — fake/mock з tests/.
 * Усі функції повертають DEV_OK (0) або від'ємний код DEV_ERR_*.
 */
#ifndef BUS_H
#define BUS_H
#include <stdint.h>
#include <stddef.h>

#define DEV_OK            0
#define DEV_ERR_BUS      (-1)   /* NACK / timeout / помилка шини */
#define DEV_ERR_ID       (-2)   /* пристрій відповів, але ID не збігається з очікуваним */
#define DEV_ERR_TIMEOUT  (-3)   /* пристрій не завершив операцію за відведений час */
#define DEV_ERR_ARG      (-4)   /* некоректні аргументи */

/** I²C: читання/запис регістрів з 8-бітною адресою регістра (7-бітна адреса пристрою). */
typedef struct {
    void *ctx;
    int (*read)(void *ctx, uint8_t addr7, uint8_t reg, uint8_t *buf, uint16_t len);
    int (*write)(void *ctx, uint8_t addr7, uint8_t reg, const uint8_t *buf, uint16_t len);
    int (*probe)(void *ctx, uint8_t addr7);   /* DEV_OK, якщо пристрій відповів ACK на адресу */
} i2c_bus_t;

/** SPI з програмним CS#: begin() опускає CS, end() піднімає; між ними — довільна кількість xfer(). */
typedef struct {
    void *ctx;
    void (*begin)(void *ctx);
    int  (*xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, uint16_t len);   /* tx або rx може бути NULL */
    void (*end)(void *ctx);
    void (*delay_ms)(uint32_t ms);
} spi_bus_t;

/** SCCB (камера): 2-фазний запис і 2-фазне читання, ACK ігнорується (див. ov7670.h). */
typedef struct {
    void *ctx;
    int (*read_reg)(void *ctx, uint8_t reg, uint8_t *val);
    int (*write_reg)(void *ctx, uint8_t reg, uint8_t val);
    void (*delay_ms)(uint32_t ms);
} sccb_bus_t;

#endif
