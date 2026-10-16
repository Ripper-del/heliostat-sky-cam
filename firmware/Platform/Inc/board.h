/**
 * @file board.h
 * @brief Єдине місце, де зібрано знання про плату: піни, канали, хендли периферії.
 *        Має відповідати docs/pinout.md. Включається тільки з Core/ і Platform/.
 *
 * NUCLEO-F411RE / F446RE, Arduino-роз'єми:
 *   USART2 TX/RX PA2/PA3 (ST-Link VCP)   LED LD2 PA5 (D13)   кнопка B1 PC13
 *   I2C1 SCL/SDA PB8/PB9 (D15/D14)       MPU-6050
 *   SCCB SIOC/SIOD PB10/PB4 (D6/D5)      OV7670 (bit-bang GPIO)
 *   MCO1 XCLK PA8 (D7)                   OV7670 XCLK 16 МГц
 *   SPI2 SCK/MISO/MOSI PB13/PB14/PB15    W25Q64;  CS PB12 (GPIO)
 *   ADC1 IN0/1/4/8 PA0/PA1/PA4/PB0       квадрант GM5528
 *   TIM3 CH1/CH2 PA6/PA7 (D12/D11)       серво азимут / елевація
 */
#ifndef BOARD_H
#define BOARD_H
#include "stm32f4xx_hal.h"

#define LED_PORT        GPIOA
#define LED_PIN         GPIO_PIN_5
#define BTN_PORT        GPIOC
#define BTN_PIN         GPIO_PIN_13
#define FLASH_CS_PORT   GPIOB
#define FLASH_CS_PIN    GPIO_PIN_12

/* SCCB камери (bit-bang): SIOC = PB10 (D6), SIOD = PB4 (D5) */
#define SCCB_PORT       GPIOB
#define SCCB_SCL_PIN    GPIO_PIN_10
#define SCCB_SDA_PIN    GPIO_PIN_4

#define ADC_CH_TL       ADC_CHANNEL_0   /* PA0 */
#define ADC_CH_TR       ADC_CHANNEL_1   /* PA1 */
#define ADC_CH_BL       ADC_CHANNEL_4   /* PA4 */
#define ADC_CH_BR       ADC_CHANNEL_8   /* PB0 */

/* Серво (TIM3, 50 Гц, 1 тік = 1 мкс). Типові межі хобі-серво, НЕ виміряні на конкретних екземплярах. */
#define SERVO_AZ_MIN_US     500U
#define SERVO_AZ_MAX_US    2500U
#define SERVO_EL_MIN_US     500U
#define SERVO_EL_MAX_US    2400U
#define SERVO_CENTER_US    1500U

extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef  hi2c1;
extern SPI_HandleTypeDef  hspi2;
extern ADC_HandleTypeDef  hadc1;

void Error_Handler(void);
#endif
