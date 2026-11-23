/** @file platform_cam.c — паралельна шина OV7670: D0..D7, PCLK, VSYNC як входи GPIO (без DCMI). */
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

int platform_cam_capture(uint8_t *y, uint32_t n_pixels, int phase, uint32_t timeout_ms)
{
    platform_cam_bus_init();
    if (!href_detected) { cam_probe_t tmp; platform_cam_probe(900, &tmp); if (!href_detected) return -1; }
    const uint32_t cyc_per_ms = SystemCoreClock / 1000U;
    const uint32_t limit = timeout_ms * cyc_per_ms;       /* ліміт очікування початку кадру */
    const uint32_t line_limit = 20U * cyc_per_ms;         /* один рядок (~1 мс) не може тривати довше */
    const uint32_t w = FRAME_LINE_PIXELS;                 /* пікселів у рядку: 160 */
    const uint32_t lines = n_pixels / w;
    uint32_t t0 = DWT->CYCCNT;
    int ok = 1;
    __disable_irq();
    while (!cam_vs())  { if (DWT->CYCCNT - t0 > limit) { ok = 0; break; } }   /* VSYNC високий: пауза між кадрами */
    while (ok && cam_vs()) { if (DWT->CYCCNT - t0 > limit) ok = 0; }          /* чекаємо спаду: початок кадру */
    for (uint32_t line = 0; ok && line < lines; line++) {
        uint32_t tl = DWT->CYCCNT;
        while (ok && !cam_href()) { if (DWT->CYCCNT - tl > line_limit) ok = 0; }
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
    }
    uint32_t elapsed = DWT->CYCCNT - t0;
    uwTick += elapsed / cyc_per_ms;       /* SysTick не працював: доганяємо мілісекунди */
    __enable_irq();
    return ok ? 0 : -1;
}
