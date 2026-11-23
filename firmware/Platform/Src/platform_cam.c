/** @file platform_cam.c — паралельна шина OV7670: D0..D7, PCLK, VSYNC як входи GPIO (без DCMI). */
#include <string.h>
#include "main.h"
#include "platform.h"

/* D0..D3 = PC0..PC3, D4..D6 = PC10..PC12, D7 = PD2: читаємо GPIOC/GPIOD один раз і збираємо байт. */
static inline uint8_t cam_data(void)
{
    uint32_t c = GPIOC->IDR, d = GPIOD->IDR;
    return (uint8_t)((c & 0x0FU) | ((c >> 6) & 0x70U) | ((d & 0x04U) << 5));
}
static inline int cam_pclk(void) { return (CAM_PCLK_PORT->IDR & CAM_PCLK_PIN) != 0; }
static inline int cam_vs(void)   { return (CAM_VS_PORT->IDR & CAM_VS_PIN) != 0; }
/* HREF: провід підключають до PC8 (CN10 пін 2) або PC9 (CN10 пін 1); який саме — визначає platform_cam_probe. */
static uint32_t href_mask = GPIO_PIN_8;
static int href_detected;
static inline int cam_href(void) { return (GPIOC->IDR & href_mask) != 0; }

void platform_cam_bus_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9; HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_2;  HAL_GPIO_Init(GPIOD, &g);
    g.Pin = CAM_PCLK_PIN; HAL_GPIO_Init(CAM_PCLK_PORT, &g);   /* PA15 після скидання = JTDI; переводимо в GPIO */
    g.Pin = CAM_VS_PIN;   HAL_GPIO_Init(CAM_VS_PORT, &g);
}

void platform_cam_probe(uint32_t window_ms, cam_probe_t *out)
{
    platform_cam_bus_init();
    out->vs_rises = out->pclk_rises = 0; out->seen_high = 0; out->seen_low = 0; out->pclk_per_frame = out->frame_us = 0;
    uint32_t pc_at_vs = 0, t_at_vs = 0; int have_vs = 0;
    uint32_t h8 = 0, h9 = 0; int h8_prev = (GPIOC->IDR >> 8) & 1, h9_prev = (GPIOC->IDR >> 9) & 1;
    uint8_t high = 0, low = 0;
    int pclk_prev = cam_pclk(), vs_prev = cam_vs();
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < window_ms) {
        int p = cam_pclk(), v = cam_vs();
        if (v && !vs_prev) {
            uint32_t now = DWT->CYCCNT;
            if (have_vs && out->pclk_per_frame == 0) {
                out->pclk_per_frame = out->pclk_rises - pc_at_vs;
                out->frame_us = (now - t_at_vs) / (SystemCoreClock / 1000000U);
            }
            pc_at_vs = out->pclk_rises; t_at_vs = now; have_vs = 1;
            out->vs_rises++;
        }
        if (p && !pclk_prev) {
            uint8_t b = cam_data();
            out->pclk_rises++;
            high |= b; low |= (uint8_t)~b;
        }
        uint32_t c = GPIOC->IDR;
        int a8 = (c >> 8) & 1, a9 = (c >> 9) & 1;
        if (a8 && !h8_prev) h8++;
        if (a9 && !h9_prev) h9++;
        h8_prev = a8; h9_prev = a9;
        pclk_prev = p; vs_prev = v;
    }
    out->seen_high = high; out->seen_low = low;
    out->href_pc8 = h8; out->href_pc9 = h9;
    if (h8 > h9 && h8 > 50) { href_mask = GPIO_PIN_8; href_detected = 1; }
    else if (h9 > 50)       { href_mask = GPIO_PIN_9; href_detected = 1; }
}

extern __IO uint32_t uwTick;    /* лічильник HAL_GetTick() */

static volatile uint32_t cam_short_frames, cam_lines_lost, cam_off_max_cyc, cam_off_n, cam_lines_last, cam_period_last, cam_resync;
uint32_t platform_cam_diag(uint32_t *off_n, uint32_t *lines, uint32_t *period_us, uint32_t *resync)
{
    *off_n = cam_off_n; *lines = cam_lines_last; *period_us = cam_period_last / (SystemCoreClock / 1000000U); *resync = cam_resync;
    return cam_off_max_cyc / (SystemCoreClock / 1000000U);
}
void platform_cam_diag_reset(void) { cam_off_max_cyc = 0; cam_off_n = 0; cam_resync = 0; cam_short_frames = 0; cam_lines_lost = 0; }
void platform_cam_loss(uint32_t *short_frames, uint32_t *lines_lost) { *short_frames = cam_short_frames; *lines_lost = cam_lines_lost; }
static int  cam_irq_mode;                         /* 0 — IRQ вимкнені на весь кадр (ЛР3); 1 — лише поки HREF активний (ЛР4) */
static void (*cam_catchup_hook)(uint32_t ms);
void platform_cam_set_irq_mode(int mode)               { cam_irq_mode = mode; }
void platform_cam_set_catchup_hook(void (*h)(uint32_t ms)) { cam_catchup_hook = h; }

int platform_cam_capture(uint8_t *y, uint32_t n_pixels, int phase, uint32_t timeout_ms)
{
    platform_cam_bus_init();
    if (!href_detected) { cam_probe_t tmp; platform_cam_probe(900, &tmp); if (!href_detected) return -1; }
    const uint32_t cyc_per_ms = SystemCoreClock / 1000U;
    const uint32_t cyc_per_us = SystemCoreClock / 1000000U;
    const uint32_t limit = timeout_ms * cyc_per_ms;       /* ліміт очікування початку кадру */
    const uint32_t line_limit = (cam_irq_mode == 1 ? 150U : 20U) * cyc_per_ms;   /* один рядок не може тривати довше; у режимі 1 задачу можуть витіснювати на десятки мс */
    const uint32_t w = FRAME_LINE_PIXELS;                 /* пікселів у рядку: 160 */
    const uint32_t lines = n_pixels / w;
    const int per_line = cam_irq_mode == 1;
    uint32_t t0 = DWT->CYCCNT;
    uint32_t line_start = 0, period = 0;                  /* такти: початок попереднього рядка і період рядків (з перших двох) */
    int ok = 1;
    uint32_t off_t0 = 0;
    if (!per_line) __disable_irq();
    while (!cam_vs())  { if (DWT->CYCCNT - t0 > limit) { ok = 0; break; } }   /* VSYNC високий: пауза між кадрами */
    while (ok && cam_vs()) { if (DWT->CYCCNT - t0 > limit) ok = 0; }          /* чекаємо спаду: початок кадру */
    uint32_t got = 0;                                     /* рядків реально захоплено */
    for (uint32_t line = 0; ok && line < lines; line++) {
        int irq_off = 0;
        if (per_line && period) {
            /* IRQ працюють майже весь період рядка; вимикаємо їх за ~250 мкс до наступного HREF: вікно = запас + активна частина рядка
             * (~640 мкс) має бути коротшим за тік (1 мс), інакше SysTick втрачає тіки. Якщо ми вже запізнились (задачу витісняли) —
             * вікно не відкриваємо наперед, чекаємо HREF з увімкненими IRQ і вимикаємо їх одразу після фронту. */
            if (DWT->CYCCNT - line_start < period - 250U * cyc_per_us) {
                while (DWT->CYCCNT - line_start < period - 250U * cyc_per_us) { }
                __disable_irq(); irq_off = 1; off_t0 = DWT->CYCCNT;
            } else cam_resync++;
        }
        uint32_t tl = DWT->CYCCNT;
        int ended = 0;
        if (irq_off) {                                       /* вікно відкрито наперед: чекаємо HREF обмежений час, інакше IRQ мають повернутись (рядок пропущено або кадр скінчився) */
            while (!cam_href()) { if (DWT->CYCCNT - off_t0 > 600U * cyc_per_us) break; }
            if (!cam_href()) { __enable_irq(); irq_off = 0; cam_resync++; off_t0 = 0; }
        }
        while (ok && !cam_href()) {
            if (DWT->CYCCNT - tl > line_limit) { ok = 0; break; }
            if (per_line && got && cam_vs()) { ended = 1; break; }      /* VSYNC піднявся: кадр скінчився раніше (частину рядків пропущено через витіснення) */
        }
        if (ended) { if (irq_off) __enable_irq(); break; }
        if (per_line && !irq_off) { __disable_irq(); irq_off = 1; off_t0 = DWT->CYCCNT; }
        uint32_t ls = DWT->CYCCNT;
        if (per_line && !period && line_start) { period = ls - line_start; if (period < 600U * cyc_per_us || period > 6000U * cyc_per_us) period = 0; }
        line_start = ls; cam_period_last = period; cam_lines_last = line + 1;
        uint32_t n = 0; int prev = 0;
        tl = DWT->CYCCNT;
        while (ok && cam_href()) {
            if (DWT->CYCCNT - tl > line_limit) { ok = 0; break; }   /* HREF завис у 1 */
            int p = cam_pclk();
            if (p && !prev) {
                uint8_t b = cam_data();
                if (n < 2 * w) {
                    if (phase == 2) y[line * 2 * w + n] = b;                   /* відладка: усі байти рядка як є */
                    else if ((n & 1U) == (uint32_t)phase) y[line * w + (n >> 1)] = b;
                }
                n++;
            }
            prev = p;
        }
        if (ok) got = line + 1;
        if (per_line) { if (off_t0) { uint32_t d = DWT->CYCCNT - off_t0; cam_off_n++; if (d > cam_off_max_cyc) cam_off_max_cyc = d; off_t0 = 0; } __enable_irq(); }
    }
    if (per_line && ok && got > 0 && got < lines) {        /* недобрані рядки (витіснення): повторюємо останній, щоб статистика кадру лишилась коректною */
        cam_short_frames++; cam_lines_lost += lines - got;
        for (uint32_t l = got; l < lines; l++) memcpy(&y[l * w], &y[(got - 1) * w], w);
    }
    uint32_t elapsed = DWT->CYCCNT - t0;
    if (!per_line) {
        uwTick += elapsed / cyc_per_ms;       /* SysTick не працював: доганяємо мілісекунди */
        if (cam_catchup_hook) cam_catchup_hook(elapsed / cyc_per_ms);
        __enable_irq();
    }
    return ok ? 0 : -1;
}
