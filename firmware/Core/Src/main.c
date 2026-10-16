/* Core/: ініціалізація периферії HAL та банер. Уся логіка — в App/, доступ до заліза — в Platform/. */
#include <stdio.h>
#include "main.h"
#include "app.h"
#include "platform.h"

UART_HandleTypeDef huart2;
I2C_HandleTypeDef  hi2c1;
SPI_HandleTypeDef  hspi2;
ADC_HandleTypeDef  hadc1;

static void SystemClock_Config(void);
static HAL_StatusTypeDef MX_GPIO_Init(void);
static HAL_StatusTypeDef MX_USART2_UART_Init(void);
static HAL_StatusTypeDef MX_I2C1_Init(void);
static HAL_StatusTypeDef MX_SPI2_Init(void);
static HAL_StatusTypeDef MX_ADC1_Init(void);
static HAL_StatusTypeDef MX_TIM3_PWM_Init(void);

static void report_init(const char *name, HAL_StatusTypeDef st)
{
    printf("[init] %-7s : %s\r\n", name, st == HAL_OK ? "HAL_OK" : "FAILED");
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    if (MX_USART2_UART_Init() != HAL_OK) Error_Handler();

    printf("\r\n=====================================\r\n");
    printf(" STM32 bring-up | LR2 | build %s %s\r\n", __DATE__, __TIME__);
    printf(" SYSCLK = %lu Hz\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    printf("=====================================\r\n");
    report_init("USART2", HAL_OK);
    platform_i2c1_recover();
    report_init("I2C1",   MX_I2C1_Init());
    report_init("SPI2",   MX_SPI2_Init());
    report_init("ADC1",   MX_ADC1_Init());
    report_init("TIM3",   MX_TIM3_PWM_Init());
    printf("[init] TIM3    : servo PWM 50 Hz, outputs OFF until enabled (PA6=AZ, PA7=EL)\r\n");
    platform_xclk(1);
    printf("[init] MCO1    : XCLK 16 MHz on PA8\r\n");

    app_init();
    while (1) app_step();
}

/* ------------------------------------------------------------------ */
/* Тактування: HSI 16 МГц -> PLL -> SYSCLK                             */
/*   F411RE: 100 МГц (PLLN=400, P=4)                                   */
/*   F446RE:  84 МГц (PLLN=336, P=4)                                   */
/* ------------------------------------------------------------------ */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM            = 16;                 /* 16 МГц / 16 = 1 МГц на вході VCO */
#if defined(STM32F446xx)
    osc.PLL.PLLN            = 336;                /* VCO = 336 МГц */
    osc.PLL.PLLQ            = 7;
    uint32_t flash_latency  = FLASH_LATENCY_2;
#else
    osc.PLL.PLLN            = 400;                /* VCO = 400 МГц */
    osc.PLL.PLLQ            = 8;
    uint32_t flash_latency  = FLASH_LATENCY_3;
#endif
    osc.PLL.PLLP            = RCC_PLLP_DIV4;      /* SYSCLK = VCO / 4 */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;           /* APB1 <= 50 МГц */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, flash_latency) != HAL_OK) Error_Handler();
}

/* PA5 — LED (вихід), PC13 — кнопка (вхід), PB12 — CS флешу (вихід, High = неактивний) */
static HAL_StatusTypeDef MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
    g.Pin = FLASH_CS_PIN; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(FLASH_CS_PORT, &g);

    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    g.Pin = LED_PIN; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &g);

    g.Pin = BTN_PIN; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BTN_PORT, &g);

    /* Квадрант фоторезисторів: аналогові входи ADC1_IN0/1/4 (PA0/1/4) та IN8 (PB0).
     * Явно переводимо в Analog, хоч це й режим за замовчуванням після Reset. */
    g.Mode = GPIO_MODE_ANALOG; g.Pull = GPIO_NOPULL;
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOB, &g);

    return HAL_OK;
}

static HAL_StatusTypeDef MX_USART2_UART_Init(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    return HAL_UART_Init(&huart2);
}

static HAL_StatusTypeDef MX_I2C1_Init(void)
{
    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 100000;          /* Standard Mode 100 кГц (з роздільним читанням стабільно 100/100) */
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2     = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    return HAL_I2C_Init(&hi2c1);
}

static HAL_StatusTypeDef MX_SPI2_Init(void)
{
    hspi2.Instance               = SPI2;
    hspi2.Init.Mode              = SPI_MODE_MASTER;
    hspi2.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi2.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi2.Init.CLKPolarity       = SPI_POLARITY_LOW;   /* SPI mode 0: CPOL=0 */
    hspi2.Init.CLKPhase          = SPI_PHASE_1EDGE;    /*             CPHA=0 */
    hspi2.Init.NSS               = SPI_NSS_SOFT;       /* CS керуємо вручну (PB12) */
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128; /* APB1/128 ~ 390 кГц — повільно,
                                                      щоб пробачити довгі/шумні проводи на макетці */
    hspi2.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi2.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi2.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi2.Init.CRCPolynomial     = 10;
    return HAL_SPI_Init(&hspi2);
}

static HAL_StatusTypeDef MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef ch = {0};
    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) return HAL_ERROR;

    ch.Channel      = ADC_CHANNEL_0;                 /* PA0 */
    ch.Rank         = 1;
    ch.SamplingTime = ADC_SAMPLETIME_84CYCLES;       /* довгий час семплу — високоомне джерело */
    return HAL_ADC_ConfigChannel(&hadc1, &ch);
}

/* TIM3 CH1/CH2 (PA6/PA7): PWM 50 Гц для серво азимуту та елевації.
 * stm32f4xx_hal_tim.c у проєкті немає (підключені лише потрібні модулі HAL),
 * тому таймер піднімаємо напряму через регістри CMSIS (RM0383), без HAL_TIM_*. */
static HAL_StatusTypeDef MX_TIM3_PWM_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOA, &g);

    __HAL_RCC_TIM3_CLK_ENABLE();

    /* TIM3CLK = 2*PCLK1 (бо APB1-дільник != 1) = SYSCLK -> ділимо до 1 МГц (1 тік = 1 мкс). */
#if defined(STM32F446xx)
    TIM3->PSC = 84 - 1;     /* 84 МГц / 84 = 1 МГц */
#else
    TIM3->PSC = 100 - 1;    /* 100 МГц / 100 = 1 МГц */
#endif
    TIM3->ARR = 20000 - 1;  /* 20000 тіків по 1 мкс = 20 мс = 50 Гц */

    /* PWM-режим 1 (OCxM = 110) + preload на CH1 (азимут) та CH2 (елевація). */
    TIM3->CCMR1 = (TIM3->CCMR1 & ~(TIM_CCMR1_OC1M | TIM_CCMR1_OC2M))
                | (TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1) | TIM_CCMR1_OC1PE
                | (TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1) | TIM_CCMR1_OC2PE;

    TIM3->CCR1 = SERVO_CENTER_US;   /* CH1 = азимут */
    TIM3->CCR2 = SERVO_CENTER_US;   /* CH2 = елевація */

    /* Виходи PWM лишаються ВИМКНЕНИМИ: серво отримують імпульси лише коли їх вмикає App (platform_servo_enable),
     * щоб не було кидка струму при старті й не тримати серво без потреби. */
    TIM3->CR1  |= TIM_CR1_ARPE;
    TIM3->EGR   = TIM_EGR_UG;       /* застосувати PSC/ARR/CCR до старту */
    TIM3->CR1  |= TIM_CR1_CEN;

    return HAL_OK;
}

/* Фатальна помилка: швидке миготіння LED. */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        for (volatile uint32_t i = 0; i < 400000; i++) {}
    }
}
