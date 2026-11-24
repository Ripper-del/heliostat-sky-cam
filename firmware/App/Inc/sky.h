/** @file sky.h — статистика кадру та класифікація стану неба (чиста логіка, без заліза; тестується на хості). */
#ifndef SKY_H
#define SKY_H
#include <stdint.h>
#include <stddef.h>

#define FRAME_W       160U
#define FRAME_H       120U
#define FRAME_PIXELS  (FRAME_W * FRAME_H)

typedef struct {
    uint8_t  mean;            /* середня яскравість Y, 0..255 */
    uint8_t  contrast;        /* середньоквадратичне відхилення Y */
    uint16_t sat_permille;    /* частка пересвічених пікселів (Y >= sat_level), проміле */
} frame_stats_t;

typedef enum { SKY_DARK = 0, SKY_OVERCAST = 1, SKY_CLEAR = 2 } sky_t;

typedef struct {
    uint8_t  dark_mean;       /* mean < dark_mean            -> DARK */
    uint8_t  sat_level;       /* піксель вважається пересвіченим при Y >= sat_level */
    uint16_t clear_sat_permille; /* частка пересвіту для CLEAR (пряме сонце в кадрі) */
    uint8_t  clear_mean;      /* або дуже яскравий кадр: mean >= clear_mean */
} sky_thresholds_t;

extern const sky_thresholds_t SKY_DEFAULTS;

/** Статистика по n пікселях яскравості; n == 0 -> нулі. */
frame_stats_t frame_stats(const uint8_t *y, size_t n, uint8_t sat_level);
/** DARK / CLEAR / інакше OVERCAST (FR-12). */
sky_t sky_classify(const frame_stats_t *s, const sky_thresholds_t *t);
const char *sky_name(sky_t s);
/** FR-16: камера й фоторезистори по-різному оцінюють темряву -> 1. */
int sky_mismatch(sky_t cam, int ldr_dark);
#endif
