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

/* Паралельна шина камери (програмне захоплення без DCMI): D0..D3 = PC0..PC3, D4..D6 = PC10..PC12, D7 = PD2,
 * PCLK = PA15, VSYNC = PB7 (HREF не підключено; камера не тактує PCLK між рядками). */
#define FRAME_LINE_PIXELS 160U     /* QQVGA: пікселів у рядку (байтів у рядку вдвічі більше) */
typedef struct {
    uint32_t vs_rises;      /* фронтів VSYNC 0->1 за вікно */
    uint32_t pclk_rises;    /* фронтів PCLK 0->1 за вікно */
    uint8_t  seen_high;     /* біти D0..D7, що хоч раз були 1 на фронті PCLK */
    uint8_t  seen_low;      /* біти, що хоч раз були 0 */
    uint32_t pclk_per_frame;/* фронтів PCLK між двома сусідніми VSYNC (0 = не виміряно) */
    uint32_t frame_us;      /* тривалість кадру між тими ж VSYNC, мкс */
    uint32_t href_pc8, href_pc9;   /* фронтів на PC8 / PC9 (HREF: там, де ~120 на кадр) */
} cam_probe_t;
void platform_cam_bus_init(void);
void platform_cam_probe(uint32_t window_ms, cam_probe_t *out);
/** Захоплення одного кадру: чекає початок кадру (спад VSYNC), далі читає байти по фронтах PCLK і лишає лише Y
 *  (байт з номером n, де (n & 1) == phase). Записує n_pixels байтів у y. Блокує процесор на весь кадр (~400 мс)
 *  з вимкненими перериваннями — інакше SysTick/UART зіб'ють відлік PCLK; лічильник HAL_GetTick() потім доганяється.
 *  Повертає 0 або -1, якщо кадр не почався за timeout_ms. */
int platform_cam_capture(uint8_t *y, uint32_t n_pixels, int phase, uint32_t timeout_ms);

/* ---- Вимірювання (ЛР3): лічильник тактів ядра DWT, службові піни для логічного аналізатора ---- */
void     platform_perf_init(void);          /* увімкнути DWT, налаштувати службові піни */
uint32_t platform_cycles(void);             /* DWT->CYCCNT (32 біти, переповнення ~43 с при 100 МГц) */
uint32_t platform_cpu_hz(void);
uint32_t platform_micros(void);             /* мкс, розширений лічильник; викликати частіше ніж раз на 40 с */
/* Службові піни: 0 = PB1 (CN10-24), 1 = PB2 (CN10-22), 2 = PB5 (CN10-29), 3 = PB6 (CN10-17), 4 = PB3 (CN10-31, реакція) */
#define PLATFORM_PROBE_COUNT 5
void     platform_probe(int ch, int level);
/* Стек: заповнення шаблоном на старті та вимір найглибшого використання */
void     platform_stack_paint(void);
uint32_t platform_stack_used(void);
uint32_t platform_stack_reserved(void);
uint32_t platform_heap_used(void);

/* Кнопка B1 по EXTI: ISR фіксує такти; задача забирає подію. 1 — була подія, *t_cycles — момент фронту. */
int      platform_button_event(uint32_t *t_cycles);
void     platform_exti_irq(void);           /* з EXTI15_10_IRQHandler */

/* UART RX через переривання в кільцевий буфер (256 Б); лічильник втрат при переповненні */
void     platform_uart_rx_irq_enable(void);
void     platform_uart2_irq(void);          /* з USART2_IRQHandler */
uint32_t platform_uart_rx_overflow(void);

/* UART: неблокуюче читання символу (-1, якщо немає) */
int platform_uart_getc(void);
#endif
