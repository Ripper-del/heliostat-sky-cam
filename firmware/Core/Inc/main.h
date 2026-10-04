/**
 * @file    main.h
 * @brief   Спільні оголошення проєкту: піни Nucleo, хендли периферії.
 *
 * Розкладка пінів (NUCLEO-F411RE / F446RE, Arduino-роз'єми):
 *
 *   Функція        Пін    Роз'єм   Примітка
 *   -------------  -----  -------  --------------------------------------
 *   USART2 TX/RX   PA2/PA3  (ST-Link VCP, дроти не потрібні)
 *   LED LD2        PA5      (вбудований зелений LED)
 *   Кнопка B1      PC13     (вбудована, активний рівень = 0)
 *   I2C1 SCL/SDA   PB8/PB9  D15/D14  MPU-6050 + SCCB камери OV7670
 *   SPI2 SCK       PB13     (W25Q)
 *   SPI2 MISO      PB14     (W25Q  DO)
 *   SPI2 MOSI      PB15     (W25Q  DI)
 *   Flash CS       PB12     (звичайний GPIO, керуємо вручну)
 *   ADC1_IN0       PA0      A0   фоторезистор GM5528 + дільник 10 кОм
 *   MCO1 (XCLK)    PA8      D7   тактовий сигнал для камери OV7670
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
