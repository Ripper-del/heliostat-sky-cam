/**
 * @file quadrant_photoresistors.c
 * @brief Квадрант із 4 фоторезисторів GM5528 (TL/TR/BL/BR) на ADC1.
 *
 * Кожен канал — дільник напруги: 3V3 --[GM5528]--+--[10k]-- GND, середня
 * точка -> ADC-пін (PA0/PA1/PA4/PB0). Чим яскравіше світло на конкретному
 * фоторезисторі, тим менший його опір і тим вища напруга на відповідному
 * ADC-вході. Різниця між каналами (TL+BL проти TR+BR, TL+TR проти BL+BR)
 * дає напрямок, куди зсунути дзеркало — сам розрахунок зсуву і керування
 * серво це завдання ЛР2+ (див. docs/PRD.md), тут лише читання АЦП.
 */
#include "main.h"
#include "selftest.h"

static uint16_t adc_read_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef ch = {0};
    ch.Channel      = channel;
    ch.Rank         = 1;
    ch.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &ch);

    uint16_t v = 0;
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) v = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return v;
}

void quadrant_read_raw(uint16_t out[4])
{
    out[0] = adc_read_channel(ADC_CH_TL);
    out[1] = adc_read_channel(ADC_CH_TR);
    out[2] = adc_read_channel(ADC_CH_BL);
    out[3] = adc_read_channel(ADC_CH_BR);
}

uint32_t photoresistor_raw_to_mv(uint16_t raw)
{
    return ((uint32_t)raw * 3300U) / 4095U;
}
