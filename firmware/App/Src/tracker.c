#include "tracker.h"
#include "quadrant.h"

void tracker_default_cfg(tracker_cfg_t *c)
{
    c->az_min_us = 500;  c->az_max_us = 2500;
    c->el_min_us = 500;  c->el_max_us = 2400;
    c->park_az_us = 1500; c->park_el_us = 1500;
    c->dark_light_thr = 1200; c->dark_time_ms = 3000;
    c->spread_permille = 40; c->deadband_permille = 30;
    c->invert_light = 0; c->sign_az = 1; c->sign_el = 1;
    c->step_us = 10;
}

void tracker_init(tracker_t *t, const tracker_cfg_t *c)
{
    t->cfg = *c;
    t->mode = MODE_TRACK;
    t->az_us = c->park_az_us; t->el_us = c->park_el_us;
    t->light = 0; t->ex = t->ey = 0; t->dark_pending = 0; t->dark_since_ms = 0;
}

uint32_t tracker_light(const uint16_t q[4], int invert)
{
    uint32_t s = 0;
    for (int i = 0; i < 4; i++) s += invert ? (uint32_t)(QUADRANT_ADC_MAX - q[i]) : q[i];
    return s;
}

static uint16_t clamp_us(int32_t v, uint16_t lo, uint16_t hi) { return (uint16_t)(v < lo ? lo : v > hi ? hi : v); }
static int32_t  iabs(int32_t v) { return v < 0 ? -v : v; }

static uint32_t spread_permille(const uint16_t q[4])
{
    uint32_t lo = q[0], hi = q[0], sum = 0;
    for (int i = 0; i < 4; i++) { if (q[i] < lo) lo = q[i]; if (q[i] > hi) hi = q[i]; sum += q[i]; }
    return sum ? (hi - lo) * 1000U * 4U / sum : 0;       /* розкид відносно середнього значення каналу */
}

const char *tracker_mode_name(track_mode_t m)
{
    return m == MODE_TRACK ? "TRACK" : m == MODE_OVERCAST ? "OVERCAST" : "PARK";
}

int tracker_update(tracker_t *t, const uint16_t q[4], sky_t sky, uint32_t now_ms)
{
    track_mode_t before = t->mode;
    const tracker_cfg_t *c = &t->cfg;
    t->light = tracker_light(q, c->invert_light);
    quadrant_error_t e = quadrant_error(q);
    t->ex = e.x_permille; t->ey = e.y_permille;

    /* Темрява: поріг із гістерезисом 1/8, щоб на межі не було брязкоту. */
    uint32_t thr = t->mode == MODE_PARK ? c->dark_light_thr + c->dark_light_thr / 8 : c->dark_light_thr;
    if (t->light < thr) {
        if (!t->dark_pending) { t->dark_pending = 1; t->dark_since_ms = now_ms; }
        if (now_ms - t->dark_since_ms >= c->dark_time_ms) t->mode = MODE_PARK;
    } else {
        t->dark_pending = 0;
    }

    if (t->mode == MODE_PARK) {
        if (!t->dark_pending) t->mode = MODE_TRACK;            /* світло повернулось: знову вибираємо між TRACK і OVERCAST */
        else { t->az_us = c->park_az_us; t->el_us = c->park_el_us; }
    }
    if (t->mode != MODE_PARK) {
        int diffuse = spread_permille(q) < c->spread_permille || sky == SKY_OVERCAST;
        t->mode = diffuse ? MODE_OVERCAST : MODE_TRACK;        /* FR-06, FR-13 */
        if (t->mode == MODE_TRACK) {
            int32_t da = iabs(t->ex) > c->deadband_permille ? (t->ex > 0 ? c->step_us : -(int32_t)c->step_us) * c->sign_az : 0;
            int32_t de = iabs(t->ey) > c->deadband_permille ? (t->ey > 0 ? c->step_us : -(int32_t)c->step_us) * c->sign_el : 0;
            t->az_us = clamp_us((int32_t)t->az_us + da, c->az_min_us, c->az_max_us);
            t->el_us = clamp_us((int32_t)t->el_us + de, c->el_min_us, c->el_max_us);
        }
    }
    return t->mode != before;
}

void loss_init(loss_t *l)
{
    l->n = l->pos = 0; l->last_event_ms = 0; l->armed = 1;
}

int loss_update(loss_t *l, uint32_t light, uint32_t now_ms)
{
    int event = 0;
    if (l->n == LOSS_HISTORY) {
        uint32_t old = l->hist[l->pos];                       /* відлік 2 с тому */
        if (old > 0 && light * 2U < old && (l->armed || now_ms - l->last_event_ms >= 5000U)) {
            event = 1; l->last_event_ms = now_ms; l->armed = 0;
        }
    }
    l->hist[l->pos] = light;
    l->pos = (uint8_t)((l->pos + 1) % LOSS_HISTORY);
    if (l->n < LOSS_HISTORY) l->n++;
    return event;
}
