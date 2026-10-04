/**
 * @file    main.c
 * @brief   ЛР1: bring-up STM32 — UART-лог, LED, кнопка, I2C/SPI/ADC + перевірка датчиків.
 *
 * Що робить прошивка:
 *   1. Налаштовує тактування, USART2, GPIO, I2C1, SPI2, ADC1, MCO1 (XCLK для камери).
 *   2. Друкує банер і результат кожної ініціалізації (HAL_OK / помилка).
 *   3. Одноразово перевіряє датчики (self-test): I2C-скан, MPU-6050, OV7670, W25Q, фоторезистор.
 *   4. У нескінченному циклі: блимає LED, щосекунди друкує "tick" + ADC, реагує на кнопку.
 */
#include <stdio.h>
#include "main.h"

UART_HandleTypeDef huart2;
I2C_HandleTypeDef  hi2c1;
SPI_HandleTypeDef  hspi2;
ADC_HandleTypeDef  hadc1;

/* Слабкі (weak) заглушки: ядро збирається саме по собі. Коли додано каталог App/,
 * його справжні реалізації з тими самими іменами переозначають ці заглушки. */
__attribute__((weak)) void     selftest_run_all(void)        { printf("[app ] no sensor probes linked\r\n"); }
__attribute__((weak)) uint16_t photoresistor_read_raw(void)  { return 0; }

static void SystemClock_Config(void);
static HAL_StatusTypeDef MX_GPIO_Init(void);
static HAL_StatusTypeDef MX_USART2_UART_Init(void);
static HAL_StatusTypeDef MX_I2C1_Init(void);
static HAL_StatusTypeDef MX_SPI2_Init(void);
static HAL_StatusTypeDef MX_ADC1_Init(void);
static void MX_MCO1_Init(void);

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
    printf(" STM32 bring-up | LR1 | build %s %s\r\n", __DATE__, __TIME__);
    printf(" SYSCLK = %lu Hz\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    printf("=====================================\r\n");
    report_init("USART2", HAL_OK);          /* якщо ви це читаєте — UART працює */
    report_init("I2C1",   MX_I2C1_Init());
    report_init("SPI2",   MX_SPI2_Init());
    report_init("ADC1",   MX_ADC1_Init());
    MX_MCO1_Init();
    printf("[init] MCO1    : XCLK 16 MHz on PA8\r\n");

    selftest_run_all();                     /* див. App/Src/selftest.c */

    uint32_t last_tick = 0;
    uint32_t seconds   = 0;
    uint8_t  btn_prev  = 1;

    while (1) {
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        HAL_Delay(100);

        /* кнопка B1: активний рівень = 0, друкуємо подію по фронту натискання */
        uint8_t btn = (uint8_t)HAL_GPIO_ReadPin(BTN_PORT, BTN_PIN);
        if (btn_prev == 1 && btn == 0) printf("[btn ] B1 pressed\r\n");
        btn_prev = btn;

        if (HAL_GetTick() - last_tick >= 1000) {
            last_tick = HAL_GetTick();
            seconds++;
            printf("tick %lu  (uptime %lu s, light ADC=%u)\r\n",
                   (unsigned long)HAL_GetTick(), (unsigned long)seconds,
                   (unsigned)photoresistor_read_raw());
        }
    }
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
    hi2c1.Init.ClockSpeed      = 100000;          /* Standard Mode 100 кГц */
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
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16; /* APB1/16 ~ 3 МГц — безпечно для старту */
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

/* MCO1 на PA8: виводимо 16 МГц (HSI) як XCLK для OV7670. Без цього камера мовчить на I2C. */
static void MX_MCO1_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin       = GPIO_PIN_8;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF0_MCO;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSI, RCC_MCODIV_1);
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
