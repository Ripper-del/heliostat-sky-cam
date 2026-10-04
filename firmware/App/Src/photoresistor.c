/**
 * @file photoresistor.c
 * @brief Фоторезистор GM5528 у дільнику напруги з 10 кОм -> PA0 (ADC1_IN0).
 *
 * Схема:  3V3 --[GM5528]--+--[10k]-- GND,  середня точка -> PA0.
 * Чим яскравіше світло, тим менший опір GM5528 і тим вища напруга на PA0.
 */
#include "main.h"
#include "selftest.h"

uint16_t photoresistor_read_raw(void)
{
    uint16_t v = 0;
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) v = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return v;
}

uint32_t photoresistor_raw_to_mv(uint16_t raw)
{
    return ((uint32_t)raw * 3300U) / 4095U;
}
