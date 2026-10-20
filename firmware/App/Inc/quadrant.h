/** @file quadrant.h — чиста логіка квадранта фоторезисторів і серво (без заліза, тестується на хості). */
#ifndef QUADRANT_H
#define QUADRANT_H
#include <stdint.h>

#define QUADRANT_ADC_MAX   4095U
#define QUADRANT_VREF_MV   3300U

typedef struct {
    int32_t x_permille;   /* >0: праві (TR+BR) яскравіші за ліві (TL+BL); діапазон -1000..+1000 */
    int32_t y_permille;   /* >0: верхні (TL+TR) яскравіші за нижні (BL+BR) */
} quadrant_error_t;

/** raw (0..4095) -> мВ на ADC-вході (Vref = 3.3 В). */
uint32_t quadrant_raw_to_mv(uint16_t raw);
/** Нормований перекіс освітленості; при нульовій сумі повертає (0,0). q = [TL, TR, BL, BR]. */
quadrant_error_t quadrant_error(const uint16_t q[4]);
/** Кут 0..180° -> ширина імпульсу в межах [min_us, max_us]; кут поза діапазоном затискається. */
uint32_t servo_angle_to_us(int deg, uint32_t min_us, uint32_t max_us);
/** Один крок плавного руху серво: наближає cur до target не більше ніж на step_us (без перельоту). */
uint32_t servo_next_step(uint32_t cur_us, uint32_t target_us, uint32_t step_us);
#endif
