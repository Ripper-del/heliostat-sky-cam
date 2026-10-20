/**
 * @file platform.h
 * @brief Публічний API шару Platform (адаптація до плати). НЕ містить заголовків HAL —
 *        його можуть включати App/ і Devices-тести. Реалізація (Platform/Src) використовує HAL.
 *
 * Тут єдине місце, де описано "що де підключено" на боці API; номери пінів — у board.h.
 */
#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdint.h>
#include "bus.h"

/* Шини для драйверів */
const i2c_bus_t  *platform_i2c1(void);     /* MPU-6050, HAL I2C1 (PB8/PB9) */
/* Відновлення I2C1: до 9 тактів SCL, поки слейв тримає SDA, + STOP; якщо периферія вже ініціалізована — DeInit/Init.
 * Викликається Core-ом перед першою ініціалізацією і автоматично після кожної невдалої транзакції. */
void platform_i2c1_recover(void);
uint32_t platform_i2c1_last_error(void);
void     platform_i2c1_set_speed(uint32_t hz);        /* перенастроїти I2C1 (діагностика) */
void     platform_i2c1_set_split_read(int on);        /* діагностика: читання без repeated-START */
void     platform_i2c1_set_retries(int n);            /* 1 = без повторів (сирі збої для статистики) */   /* HAL_I2C_ERROR_* останньої невдачі (0 = не було) */
const spi_bus_t  *platform_spi2_flash(void);/* W25Q64, HAL SPI2 + CS=PB12 (mode 0) */
const sccb_bus_t *platform_sccb_cam(void); /* OV7670, bit-bang на PB10 (SIOC) / PB4 (SIOD) */

/* Квадрант фоторезисторів: [TL, TR, BL, BR], 0..4095 */
void platform_adc_read_quadrant(uint16_t out[4]);

/* Серво: канали 0 = азимут (SG92R, PA6), 1 = елевація (SG90, PA7). Значення затискається в межі. */
#define PLATFORM_SERVO_AZ 0
#define PLATFORM_SERVO_EL 1
void     platform_servo_set_us(int ch, uint32_t us);
void     platform_servo_enable(int ch, int on);   /* on = 1: подавати імпульси; 0: вихід вимкнено (серво вільне, струм мінімальний) */
int      platform_servo_is_enabled(int ch);
uint32_t platform_servo_get_us(int ch);
uint32_t platform_servo_min_us(int ch);
uint32_t platform_servo_max_us(int ch);

/* Час, LED, кнопка, службовий GPIO-маркер (PA5/D13 — LED, зручно чіпляти аналізатор) */
uint32_t platform_millis(void);
void     platform_delay_ms(uint32_t ms);
void     platform_led_toggle(void);
void     platform_marker(int level);
int      platform_button_pressed(void);

/* XCLK камери (MCO1/PA8, HSI 16 МГц): 1 = подавати, 0 = вимкнути (PA8 = GPIO низько) */
void platform_xclk(int on);
int  platform_xclk_is_on(void);

/* UART: неблокуюче читання символу (-1, якщо немає) */
int platform_uart_getc(void);
#endif
