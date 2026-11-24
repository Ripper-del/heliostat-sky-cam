/**
 * @file main.h
 * @brief Заголовок Core/: підтягує HAL і опис плати (Platform/Inc/board.h).
 *        Розкладка пінів — у board.h та docs/pinout.md.
 */
#ifndef MAIN_H
#define MAIN_H
#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "board.h"

#ifdef __cplusplus
}
#endif
#endif /* MAIN_H */
