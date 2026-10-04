/**
 * @file    main.h
 * @brief   Спільні оголошення проєкту: піни Nucleo, хендли периферії.
 *
 * Проєкт: геліостат (Heliostat) — квадрант фоторезисторів наводить дзеркало
 * (азимут + елевація) на джерело світла, камера OV7670 класифікує стан неба.
 *
 * Розкладка пінів (NUCLEO-F411RE / F446RE, Arduino-роз'єми):
 *
 *   Функція          Пін     Роз'єм   Примітка
 *   ---------------  ------  -------  --------------------------------------
 *   USART2 TX/RX     PA2/PA3          ST-Link VCP, дроти не потрібні
 *   LED LD2          PA5              вбудований зелений LED
 *   Кнопка B1        PC13             вбудована, активний рівень = 0
 *   I2C1 SCL/SDA     PB8/PB9  D15/D14 MPU-6050 (опційно) + SCCB камери OV7670
 *   SPI2 SCK         PB13             W25Q64 CLK
 *   SPI2 MISO        PB14             W25Q64 DO
 *   SPI2 MOSI        PB15             W25Q64 DI
 *   Flash CS         PB12             звичайний GPIO, керуємо вручну
 *   ADC1_IN0 (TL)    PA0      A0      фоторезистор, верхній-лівий квадрант
 *   ADC1_IN1 (TR)    PA1      A1      фоторезистор, верхній-правий квадрант
 *   ADC1_IN4 (BL)    PA4      A2      фоторезистор, нижній-лівий квадрант
 *   ADC1_IN8 (BR)    PB0      A3      фоторезистор, нижній-правий квадрант
 *   TIM3 CH1 (AZ)    PA6      D12     PWM 50 Гц, серво азимуту (SG92R)
 *   TIM3 CH2 (EL)    PA7      D11     PWM 50 Гц, серво елевації (SG90)
 *   MCO1 (XCLK)      PA8      D7      тактовий сигнал для камери OV7670
 *
 * Кожен канал квадранта — дільник 3V3 -> GM5528 -> (ADC-пін) -> 10 кОм -> GND.
 * Серво живляться від 5V плати (не від 3V3!), GND спільний з платою.
 */
#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* ---- Піни ---- */
#define LED_PORT        GPIOA
#define LED_PIN         GPIO_PIN_5
#define BTN_PORT        GPIOC
#define BTN_PIN         GPIO_PIN_13
#define FLASH_CS_PORT   GPIOB
#define FLASH_CS_PIN    GPIO_PIN_12

/* ---- Квадрант фоторезисторів: канали ADC1 (порядок TL, TR, BL, BR) ---- */
#define ADC_CH_TL       ADC_CHANNEL_0   /* PA0 */
#define ADC_CH_TR       ADC_CHANNEL_1   /* PA1 */
#define ADC_CH_BL       ADC_CHANNEL_4   /* PA4 */
#define ADC_CH_BR       ADC_CHANNEL_8   /* PB0 */

/* ---- Серво (TIM3, PWM 50 Гц, 1 тік = 1 мкс) ----
 * Значення нижче — типові для аналогових хобі-серво (500..2500 мкс), узяті як
 * стартова точка, а НЕ виміряні на конкретному екземплярі SG92R/SG90. Реальну
 * калібровку (мін./макс. кут без заклинювання рога) треба виконати вручну на
 * залізі — це завдання наступних ЛР (CLI-команда калібрування, FR у PRD). */
#define SERVO_AZ_MIN_US     500U    /* SG92R, азимут: нижня межа імпульсу, мкс */
#define SERVO_AZ_MAX_US    2500U    /* SG92R, азимут: верхня межа імпульсу, мкс */
#define SERVO_EL_MIN_US     500U    /* SG90,  елевація: нижня межа імпульсу, мкс */
#define SERVO_EL_MAX_US    2400U    /* SG90,  елевація: верхня межа імпульсу, мкс (дещо вужчий діапазон) */
#define SERVO_CENTER_US    1500U    /* нейтральне положення для обох серв */

/* ---- Глобальні хендли HAL (визначені в main.c) ---- */
extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef  hi2c1;
extern SPI_HandleTypeDef  hspi2;
extern ADC_HandleTypeDef  hadc1;

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
