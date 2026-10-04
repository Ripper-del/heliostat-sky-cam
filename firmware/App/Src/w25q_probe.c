/**
 * @file w25q_probe.c
 * @brief Читання JEDEC ID SPI-флешу W25Qxx (команда 0x9F).
 *
 * На платі встановлено Winbond W25Q64JV-IQ (маркування на корпусі: 25Q64JVSIQ).
 * Очікувана відповідь: EF 40 17. Якщо модуль виявиться варіантом -IM (не -IQ),
 * буде EF 70 17 — третій байт (ємність, 17 = 64 Мбіт) той самий, другий зміниться.
 *   EF = Winbond, 40/70 = тип інтерфейсу (SPI NOR, стандартний/-IM), 17 = ємність 64 Мбіт.
 */
#include "main.h"
#include "selftest.h"

#define W25_CMD_RELEASE_PD  0xAB
#define W25_CMD_JEDEC_ID    0x9F

static inline void cs_low(void)  { HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET); }
static inline void cs_high(void) { HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET); }

int w25q_read_jedec(uint8_t id[3])
{
    uint8_t cmd;

    /* Вивести з режиму power-down (на випадок, якщо чіп там). */
    cmd = W25_CMD_RELEASE_PD;
    cs_low();
    HAL_StatusTypeDef s1 = HAL_SPI_Transmit(&hspi2, &cmd, 1, 50);
    cs_high();
    HAL_Delay(1);

    cmd = W25_CMD_JEDEC_ID;
    cs_low();
    HAL_StatusTypeDef s2 = HAL_SPI_Transmit(&hspi2, &cmd, 1, 50);
    HAL_StatusTypeDef s3 = HAL_SPI_Receive(&hspi2, id, 3, 50);
    cs_high();

    return (s1 == HAL_OK && s2 == HAL_OK && s3 == HAL_OK) ? 0 : -1;
}
