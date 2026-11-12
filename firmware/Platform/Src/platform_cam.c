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
    g.Pin = GPIO_PIN_2;  HAL_GPIO_Init(GPIOD, &g);
    g.Pin = CAM_PCLK_PIN; HAL_GPIO_Init(CAM_PCLK_PORT, &g);   /* PA15 після скидання = JTDI; переводимо в GPIO */
    g.Pin = CAM_VS_PIN;   HAL_GPIO_Init(CAM_VS_PORT, &g);
}

void platform_cam_probe(uint32_t window_ms, cam_probe_t *out)
{
    platform_cam_bus_init();
    out->vs_rises = out->pclk_rises = 0; out->seen_high = 0; out->seen_low = 0; out->pclk_per_frame = out->frame_us = 0;
    uint32_t pc_at_vs = 0, t_at_vs = 0; int have_vs = 0;
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
        pclk_prev = p; vs_prev = v;
    }
    out->seen_high = high; out->seen_low = low;
}
