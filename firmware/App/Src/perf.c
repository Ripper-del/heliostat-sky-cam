#include <string.h>
#include "perf.h"

void perf_stat_init(perf_stat_t *s, const char *name)
{
    memset(s, 0, sizeof *s);
    s->name = name;
}

void perf_add(perf_stat_t *s, uint32_t v)
{
    if (s->count == 0 || v < s->min) s->min = v;
    if (v > s->max) s->max = v;
    s->sum += v;
    s->count++;
}

uint32_t perf_avg(const perf_stat_t *s) { return s->count ? (uint32_t)(s->sum / s->count) : 0; }

void perf_hist_init(perf_hist_t *h, uint32_t bin_width)
{
    memset(h, 0, sizeof *h);
    h->bin_width = bin_width ? bin_width : 1;
}

void perf_hist_add(perf_hist_t *h, uint32_t v)
{
    uint32_t i = v / h->bin_width;
    if (i >= PERF_HIST_BINS) i = PERF_HIST_BINS - 1;
    h->bins[i]++;
}

uint32_t perf_hist_percentile(const perf_hist_t *h, uint32_t pct)
{
    uint32_t total = 0;
    for (int i = 0; i < PERF_HIST_BINS; i++) total += h->bins[i];
    if (total == 0) return 0;
    uint64_t need = ((uint64_t)total * pct + 99) / 100, acc = 0;
    for (int i = 0; i < PERF_HIST_BINS; i++) {
        acc += h->bins[i];
        if (acc >= need) return (uint32_t)(i + 1) * h->bin_width;
    }
    return PERF_HIST_BINS * h->bin_width;
}
