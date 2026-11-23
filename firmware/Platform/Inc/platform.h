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
/** ЛР4: ОС реєструє затримку, що віддає процесор іншим задачам (vTaskDelay), замість активного очікування HAL_Delay. 0 = HAL_Delay (ЛР3). */
void     platform_set_delay_hook(void (*h)(uint32_t ms));
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
/** Режим переривань під час захоплення: 0 — вимкнені на весь кадр (ЛР3); 1 — вимкнені лише поки HREF активний (~0,3 мс на рядок),
 *  між рядками ISR і планувальник ОС працюють. */
void platform_cam_set_irq_mode(int mode);
/** Діагностика режиму 1: найдовше вікно з вимкненими IRQ (мкс), кількість вікон, період рядка (мкс), скільки разів вікно відкрито запізно. */
uint32_t platform_cam_diag(uint32_t *off_n, uint32_t *lines, uint32_t *period_us, uint32_t *resync);
void     platform_cam_diag_reset(void);
void     platform_cam_loss(uint32_t *short_frames, uint32_t *lines_lost);   /* кадрів, де частину рядків пропущено через витіснення, і скільки рядків не добрано */
/** ОС реєструє функцію, якою доганяється лічильник тіків після кадру з вимкненими перериваннями (режим 0). */
void platform_cam_set_catchup_hook(void (*h)(uint32_t ms));

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
uint32_t platform_button_overruns(void);    /* події, що злились із непрочитаною (втрачені) */

/* ЛР4: замість прапорця/кільцевого буфера ISR викликає зареєстровану функцію (черга/stream buffer ядра ОС). 0 = поведінка ЛР3. */
void     platform_set_button_hook(void (*h)(uint32_t isr_cyc, uint32_t evt_cyc));     /* isr_cyc — вхід у ISR (DWT); evt_cyc — справжній момент події: для імітатора апаратне переповнення TIM2 (враховує затримку ISR через вимкнені IRQ), інакше = isr_cyc */
void     platform_set_uart_hook(void (*h)(uint8_t b));
/** Імітатор кнопки: кожні period_ms мс TIM2 ставить програмну EXTI-подію лінії кнопки; 0 — вимкнути. Для сценарію S3 без людини. */
void     platform_btnsim(uint32_t period_ms);
void     platform_tim2_irq(void);
/** Інжекція відмови: 1 — SDA I2C1 (PB9) замкнено на землю, 0 — відпущено. */
void     platform_fault_sda(int on);          /* з TIM2_IRQHandler */

/* UART RX через переривання в кільцевий буфер (256 Б); лічильник втрат при переповненні */
void     platform_uart_rx_irq_enable(void);
void     platform_uart2_irq(void);          /* з USART2_IRQHandler */
void     platform_dma1_s5_irq(void);        /* DMA1 Stream5: приймання USART2 */
void     platform_dma1_s6_irq(void);        /* DMA1 Stream6: передавання USART2 */
int      platform_uart_write(const uint8_t *p, int len);   /* у кільце передавання, DMA відправляє */
void     platform_uart_flush_polling(void); /* аварійно: відправити все з кільця без переривань */
uint32_t platform_uart_rx_overflow(void);
uint32_t platform_uart_eol_cycles(void);   /* такти, коли в ISR прийшов останній Enter (початок відліку затримки команди) */

/* UART: неблокуюче читання символу (-1, якщо немає) */
int platform_uart_getc(void);
#endif
