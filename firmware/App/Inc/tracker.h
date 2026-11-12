/**
 * @file tracker.h — режими TRACK / OVERCAST / PARK і цілі серво за квадрантом (FR-02…06, 13). Чиста логіка.
 * Викликається з циклу кожні 10 мс; сама серво не рухає, лише віддає цільові імпульси.
 */
#ifndef TRACKER_H
#define TRACKER_H
#include <stdint.h>
#include "sky.h"

typedef enum { MODE_TRACK = 0, MODE_OVERCAST = 1, MODE_PARK = 2 } track_mode_t;

typedef struct {
    uint16_t az_min_us, az_max_us, el_min_us, el_max_us;   /* калібровані межі імпульсу (FR-04) */
    uint16_t park_az_us, park_el_us;                      /* безпечне положення (FR-05) */
    uint32_t dark_light_thr;      /* сума чотирьох каналів у "світлових" одиницях нижче порога = темно */
    uint32_t dark_time_ms;        /* скільки темно має бути, щоб піти в PARK */
    uint16_t spread_permille;     /* розкид квадрантів нижче цього = розсіяне світло (OVERCAST) */
    uint16_t deadband_permille;   /* |зсув| менший за це — серво не рухаємо */
    int8_t   invert_light;        /* 1: АЦП зростає при зменшенні освітленості */
    int8_t   sign_az, sign_el;    /* напрямок руху серво: +1 або -1 */
    uint8_t  step_us;             /* макс. зміна цілі за один виклик (10 мс) */
    int16_t  ex_off, ey_off;      /* нуль квадранта, проміле (статичний перекіс датчиків; CAL ZERO) */
} tracker_cfg_t;

typedef struct {
    tracker_cfg_t cfg;
    track_mode_t  mode;
    uint16_t      az_us, el_us;   /* поточні цілі серво */
    uint32_t      light;          /* остання сума освітленості */
    int32_t       ex, ey;         /* останній зсув, проміле */
    uint8_t       dark_pending;
    uint32_t      dark_since_ms;
} tracker_t;

void tracker_default_cfg(tracker_cfg_t *c);
void tracker_init(tracker_t *t, const tracker_cfg_t *c);
/** Сума "світла" з урахуванням полярності: більше = яскравіше. */
uint32_t tracker_light(const uint16_t q[4], int invert);
/** Повертає 1, якщо режим змінився. sky — остання класифікація камери (FR-13). */
int tracker_update(tracker_t *t, const uint16_t q[4], sky_t sky, uint32_t now_ms);
const char *tracker_mode_name(track_mode_t m);

/** FR-14: падіння суми більш ніж на 50 % за 2 с. */
#define LOSS_HISTORY 20          /* 20 відліків по 100 мс = 2 с */
typedef struct { uint32_t hist[LOSS_HISTORY]; uint8_t n, pos; uint32_t last_event_ms; uint8_t armed; } loss_t;
void loss_init(loss_t *l);
/** Викликати раз на 100 мс. 1 — подія "втрата сигналу" (не частіше ніж раз на 5 с). */
int  loss_update(loss_t *l, uint32_t light, uint32_t now_ms);
#endif
