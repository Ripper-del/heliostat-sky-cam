/** @file platform_buses.c — реалізація інтерфейсів bus.h поверх HAL (I2C1, SPI2) та bit-bang SCCB. */
#include <stdio.h>
#include "main.h"
#include "platform.h"

#define OV7670_ADDR_W  0x42   /* 7-бітна 0x21 << 1, див. Devices/ov7670.h */

/* ---------------- I2C1 (MPU-6050) ---------------- */
static int i2c_retries = 3;
/* Читання двома обмінами (запис регістра + STOP, потім читання) замість HAL_I2C_Mem_Read з repeated-START:
 * на F411 Mem_Read давав 66..94 % успіху, split — 94..100 % (docs/problems.md, проблема 6). */
static int i2c_split_read = 1;
#define I2C_RETRIES i2c_retries
static uint32_t i2c_last_err;
static int i2c_inited;     /* HAL_I2C_Init виконано (Core) — тоді при відновленні потрібен DeInit/Init */

/* Стандартне відновлення шини (UM10204, п. 3.1.16): слейв, що не скидається разом з МК або
 * після збійної транзакції, може тримати SDA в 0. Тактуємо SCL до 9 разів, поки SDA не звільниться, потім STOP. */
void platform_i2c1_recover(void)
{
    if (i2c_inited) HAL_I2C_DeInit(&hi2c1);
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9; g.Mode = GPIO_MODE_OUTPUT_OD; g.Pull = GPIO_PULLUP; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
    HAL_Delay(1);
    int sda0 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9), scl0 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8), pulses = 0;
    while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET && pulses < 9) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET); HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);   HAL_Delay(1);
        pulses++;
    }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET); HAL_Delay(1);   /* STOP: SDA 0 -> 1 при SCL = 1 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);   HAL_Delay(1);
    printf("[i2c1] bus: idle SDA=%d SCL=%d, recovery clocks=%d, after SDA=%d\r\n",
           sda0, scl0, pulses, HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9));
    if (i2c_inited) HAL_I2C_Init(&hi2c1);
}

uint32_t platform_i2c1_last_error(void) { return i2c_last_err; }
void platform_i2c1_set_split_read(int on) { i2c_split_read = on; }
void platform_i2c1_set_retries(int n) { i2c_retries = n < 1 ? 1 : n; }

void platform_i2c1_set_speed(uint32_t hz)
{
    HAL_I2C_DeInit(&hi2c1);
    hi2c1.Init.ClockSpeed = hz;
    HAL_I2C_Init(&hi2c1);
    i2c_inited = 1;
}

static int i2c_check(HAL_StatusTypeDef st)
{
    if (st == HAL_OK) return DEV_OK;
    i2c_last_err = HAL_I2C_GetError(&hi2c1);
    if (hi2c1.State != HAL_I2C_STATE_READY || (i2c_last_err & ~HAL_I2C_ERROR_AF)) {   /* AF = просто NACK */
        i2c_inited = 1;
        platform_i2c1_recover();
    }
    return DEV_ERR_BUS;
}

static int i2c_read(void *ctx, uint8_t addr7, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)ctx; i2c_inited = 1;
    int rc = DEV_ERR_BUS;      /* до 3 спроб: на макетці трапляються поодинокі збої ACK (проблема 6) */
    for (int i = 0; i < I2C_RETRIES && rc != DEV_OK; i++)
    {
        HAL_StatusTypeDef st;
        if (i2c_split_read) {
            st = HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(addr7 << 1), &reg, 1, 50);
            if (st == HAL_OK) st = HAL_I2C_Master_Receive(&hi2c1, (uint16_t)(addr7 << 1), buf, len, 50);
        } else {
            st = HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(addr7 << 1), reg, I2C_MEMADD_SIZE_8BIT, buf, len, 50);
        }
        rc = i2c_check(st);
    }
    return rc;
}

static int i2c_write(void *ctx, uint8_t addr7, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)ctx; i2c_inited = 1;
    int rc = DEV_ERR_BUS;
    for (int i = 0; i < I2C_RETRIES && rc != DEV_OK; i++)
        rc = i2c_check(HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(addr7 << 1), reg, I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len, 50));
    return rc;
}

/* Проба адреси: запис одного байта-вказівника (0x00). HAL_I2C_IsDeviceReady після серії NACK на порожніх
 * адресах ненадійний (залишає блок у BUSY), тому використовуємо звичайну передачу; після NACK — скидання блоку. */
static int i2c_probe(void *ctx, uint8_t addr7)
{
    (void)ctx; i2c_inited = 1;
    uint8_t zero = 0;
    if (HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(addr7 << 1), &zero, 1, 10) == HAL_OK) return DEV_OK;
    if (hi2c1.State != HAL_I2C_STATE_READY) { HAL_I2C_DeInit(&hi2c1); HAL_I2C_Init(&hi2c1); }
    return DEV_ERR_BUS;
}

static const i2c_bus_t i2c1_bus = { NULL, i2c_read, i2c_write, i2c_probe };
const i2c_bus_t *platform_i2c1(void) { return &i2c1_bus; }

/* ---------------- SPI2 (W25Q64), CS# програмний ---------------- */
static void spi_begin(void *ctx) { (void)ctx; HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET); }
static void spi_end(void *ctx)   { (void)ctx; HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET); }

static int spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    (void)ctx;
    HAL_StatusTypeDef st;
    if (tx && rx)  st = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)tx, rx, len, 100);
    else if (tx)   st = HAL_SPI_Transmit(&hspi2, (uint8_t *)tx, len, 100);
    else {
        /* Лише приймання: HAL_SPI_Receive у режимі master/2 лінії сам викликає TransmitReceive(rx, rx) і
         * висилає на MOSI вміст буфера приймання (на аналізаторі було видно сміття: 08, 03 …).
         * Тому явно висилаємо dummy 0xFF — детермінований MOSI. */
        static const uint8_t dummy[32] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        st = HAL_OK;
        for (uint16_t off = 0; off < len && st == HAL_OK; ) {
            uint16_t left = (uint16_t)(len - off);
            uint16_t n = left > (uint16_t)sizeof dummy ? (uint16_t)sizeof dummy : left;
            st = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)dummy, rx + off, n, 100);
            off = (uint16_t)(off + n);
        }
    }
    return st == HAL_OK ? DEV_OK : DEV_ERR_BUS;
}

static const spi_bus_t spi2_bus = { NULL, spi_begin, spi_xfer, spi_end, platform_delay_ms };
const spi_bus_t *platform_spi2_flash(void) { return &spi2_bus; }

/* ---------------- SCCB (OV7670), bit-bang ----------------
 * Чому не HAL_I2C: ACK-біт у SCCB "don't care", немає repeated-START, а PB4/PB10 не є піни I2C1.
 * Кожна зміна лінії + 10 мкс затримки => на записі період ~31 мкс (~32 кГц); затримка по лічильнику DWT (не залежить від SYSCLK). */
static void sccb_dly(void)
{
    uint32_t t = DWT->CYCCNT, n = (SystemCoreClock / 1000000U) * 10U;
    while ((DWT->CYCCNT - t) < n) {}
}
static void scl(int v) { HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, v ? GPIO_PIN_SET : GPIO_PIN_RESET); sccb_dly(); }
static void sda(int v) { HAL_GPIO_WritePin(SCCB_PORT, SCCB_SDA_PIN, v ? GPIO_PIN_SET : GPIO_PIN_RESET); sccb_dly(); }
static int  rd_sda(void) { return HAL_GPIO_ReadPin(SCCB_PORT, SCCB_SDA_PIN) == GPIO_PIN_SET; }
static int  rd_scl(void) { return HAL_GPIO_ReadPin(SCCB_PORT, SCCB_SCL_PIN) == GPIO_PIN_SET; }

static void sccb_pins_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = SCCB_SCL_PIN | SCCB_SDA_PIN;
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_PULLUP;                 /* внутрішній pull-up; на макетці додатково зовнішні резистори */
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SCCB_PORT, &g);
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN | SCCB_SDA_PIN, GPIO_PIN_SET);
    sccb_dly();
    /* Камера не скидається разом з МК і може стояти посеред старого обміну: 9 тактів при SDA=1 + STOP
     * повертають її конечний автомат у початковий стан. */
    for (int i = 0; i < 9; i++) { scl(0); scl(1); }
    sda(0); scl(1); sda(1);
}

static void s_start(void) { sda(1); scl(1); sda(0); scl(0); }
static void s_stop(void)  { sda(0); scl(1); sda(1); }

static void s_wr_byte(uint8_t b)          /* 9-й біт: відпускаємо SDA, ACK не перевіряємо (SCCB) */
{
    for (int i = 7; i >= 0; i--) { sda((b >> i) & 1); scl(1); scl(0); }
    sda(1); scl(1); scl(0);
}

static uint8_t s_rd_byte(void)
{
    uint8_t v = 0;
    sda(1);
    for (int i = 0; i < 8; i++) { scl(1); v = (uint8_t)((v << 1) | rd_sda()); scl(0); }
    sda(1); scl(1); scl(0);               /* master NACK */
    return v;
}

static int sccb_read_reg(void *ctx, uint8_t reg, uint8_t *val)
{
    (void)ctx;
    sccb_pins_init();
    if (!rd_sda() || !rd_scl()) return DEV_ERR_BUS;      /* лінія в 0: замикання/несправність */
    s_start(); s_wr_byte((uint8_t)(OV7670_ADDR_W)); s_wr_byte(reg); s_stop();   /* 2-фазний запис адреси регістра */
    sccb_dly(); sccb_dly();
    s_start(); s_wr_byte((uint8_t)(OV7670_ADDR_W | 1)); *val = s_rd_byte(); s_stop(); /* 2-фазне читання */
    return DEV_OK;
}

static int sccb_write_reg(void *ctx, uint8_t reg, uint8_t val)
{
    (void)ctx;
    sccb_pins_init();
    if (!rd_sda() || !rd_scl()) return DEV_ERR_BUS;
    s_start(); s_wr_byte(OV7670_ADDR_W); s_wr_byte(reg); s_wr_byte(val); s_stop();   /* 3-фазний запис */
    return DEV_OK;
}

static const sccb_bus_t sccb_cam = { NULL, sccb_read_reg, sccb_write_reg, platform_delay_ms };
const sccb_bus_t *platform_sccb_cam(void) { return &sccb_cam; }
