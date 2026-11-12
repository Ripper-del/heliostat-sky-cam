/** @file perf.h — статистика вимірювань (min/avg/max, лічильник) і гістограма. Без заліза: час передає виклик. */
#ifndef PERF_H
#define PERF_H
#include <stdint.h>

typedef struct { const char *name; uint32_t count, min, max; uint64_t sum; } perf_stat_t;
void     perf_stat_init(perf_stat_t *s, const char *name);
void     perf_add(perf_stat_t *s, uint32_t v);
uint32_t perf_avg(const perf_stat_t *s);

#define PERF_HIST_BINS 16
typedef struct { uint32_t bin_width; uint32_t bins[PERF_HIST_BINS]; } perf_hist_t;   /* остання комірка — все, що більше */
void perf_hist_init(perf_hist_t *h, uint32_t bin_width);
void perf_hist_add(perf_hist_t *h, uint32_t v);
/** Значення, нижче якого лежить pct % вибірки (верхня межа комірки); 0, якщо порожньо. */
uint32_t perf_hist_percentile(const perf_hist_t *h, uint32_t pct);
#endif
