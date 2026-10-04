/**
 * @file stm32f4xx_hal_msp.c
 * @brief MSP = MCU Support Package: низькорівнева ініціалізація пінів і тактування.
 *
 * HAL_xxx_Init() у кінці викликає HAL_xxx_MspInit(). Тут ми вмикаємо
 * тактування порту/периферії та призначаємо піну альтернативну функцію (AF).
 * Саме це CubeMX генерує, коли ви клацаєте пін на схемі.
 */
#include "main.h"

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        g.Pin       = GPIO_PIN_2 | GPIO_PIN_3;      /* PA2=TX, PA3=RX */
        g.Mode      = GPIO_MODE_AF_PP;
        g.Pull      = GPIO_PULLUP;
        g.Speed     = GPIO_SPEED_FREQ_LOW;
        g.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &g);
    }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1) {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_GPIOB_CLK_ENABLE();
        g.Pin       = GPIO_PIN_8 | GPIO_PIN_9;      /* PB8=SCL, PB9=SDA */
        g.Mode      = GPIO_MODE_AF_OD;              /* open-drain — обов'язково для I2C */
        g.Pull      = GPIO_PULLUP;                  /* внутрішній pull-up (слабкий; на модулях є свої) */
        g.Speed     = GPIO_SPEED_FREQ_HIGH;
        g.Alternate = GPIO_AF4_I2C1;
        HAL_GPIO_Init(GPIOB, &g);
        __HAL_RCC_I2C1_CLK_ENABLE();
    }
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI2) {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_SPI2_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        g.Pin       = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15; /* SCK, MISO, MOSI */
        g.Mode      = GPIO_MODE_AF_PP;
        g.Pull      = GPIO_NOPULL;
        g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF5_SPI2;
        HAL_GPIO_Init(GPIOB, &g);
    }
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        g.Pin  = GPIO_PIN_0;                        /* PA0 = ADC1_IN0 */
        g.Mode = GPIO_MODE_ANALOG;
        g.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &g);
    }
}
