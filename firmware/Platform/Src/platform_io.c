/** @file platform_io.c — ADC-квадрант, серво (TIM3), час, LED/кнопка, UART RX. */
#include "main.h"
#include "platform.h"

/* ---- ADC ---- */
static uint16_t adc_read_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef ch = {0};
    ch.Channel = channel; ch.Rank = 1; ch.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &ch);
    uint16_t v = 0;
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) v = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return v;
}

void platform_adc_read_quadrant(uint16_t out[4])
{
    out[0] = adc_read_channel(ADC_CH_TL);
    out[1] = adc_read_channel(ADC_CH_TR);
    out[2] = adc_read_channel(ADC_CH_BL);
    out[3] = adc_read_channel(ADC_CH_BR);
}

/* ---- Серво: TIM3 CCR1/CCR2, 1 тік = 1 мкс (налаштовує Core/main.c) ---- */
uint32_t platform_servo_min_us(int ch) { return ch == PLATFORM_SERVO_AZ ? SERVO_AZ_MIN_US : SERVO_EL_MIN_US; }
uint32_t platform_servo_max_us(int ch) { return ch == PLATFORM_SERVO_AZ ? SERVO_AZ_MAX_US : SERVO_EL_MAX_US; }

void platform_servo_set_us(int ch, uint32_t us)
{
    uint32_t lo = platform_servo_min_us(ch), hi = platform_servo_max_us(ch);
    if (us < lo) us = lo;
    if (us > hi) us = hi;
    if (ch == PLATFORM_SERVO_AZ) TIM3->CCR1 = us; else TIM3->CCR2 = us;
}

void platform_servo_enable(int ch, int on)
{
    uint32_t bit = (ch == PLATFORM_SERVO_AZ) ? TIM_CCER_CC1E : TIM_CCER_CC2E;
    if (on) TIM3->CCER |= bit; else TIM3->CCER &= ~bit;
}

int platform_servo_is_enabled(int ch)
{
    return (TIM3->CCER & ((ch == PLATFORM_SERVO_AZ) ? TIM_CCER_CC1E : TIM_CCER_CC2E)) != 0;
}

uint32_t platform_servo_get_us(int ch) { return ch == PLATFORM_SERVO_AZ ? TIM3->CCR1 : TIM3->CCR2; }

/* ---- Час, LED, кнопка, маркер ---- */
uint32_t platform_millis(void)         { return HAL_GetTick(); }
void     platform_delay_ms(uint32_t ms){ HAL_Delay(ms); }
void     platform_led_toggle(void)     { HAL_GPIO_TogglePin(LED_PORT, LED_PIN); }
void     platform_marker(int level)    { HAL_GPIO_WritePin(LED_PORT, LED_PIN, level ? GPIO_PIN_SET : GPIO_PIN_RESET); }
int      platform_button_pressed(void) { return HAL_GPIO_ReadPin(BTN_PORT, BTN_PIN) == GPIO_PIN_RESET; }

/* ---- UART RX (неблокуюче) ---- */
int platform_uart_getc(void)
{
    uint8_t c;
    if (HAL_UART_Receive(&huart2, &c, 1, 0) == HAL_OK) return c;
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    return -1;
}

/* ---- XCLK камери: MCO1 на PA8, джерело HSI 16 МГц (діапазон камери 10..48 МГц) ---- */
static int xclk_on;
int platform_xclk_is_on(void) { return xclk_on; }

void platform_xclk(int on)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin = GPIO_PIN_8; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    if (on) {
        g.Mode = GPIO_MODE_AF_PP; g.Alternate = GPIO_AF0_MCO;
        HAL_GPIO_Init(GPIOA, &g);
        HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSI, RCC_MCODIV_1);
    } else {
        g.Mode = GPIO_MODE_OUTPUT_PP;
        HAL_GPIO_Init(GPIOA, &g);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
    }
    xclk_on = on;
}
