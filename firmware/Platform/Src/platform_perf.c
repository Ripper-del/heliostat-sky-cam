/** @file platform_perf.c — DWT, час у мкс, службові піни, стек/heap, кнопка (EXTI), UART RX (IRQ). */
#include <malloc.h>
#include "main.h"
#include "platform.h"
#include "ringbuf.h"

/* Пріоритет ISR кнопки/UART/TIM2: у ЛР4 має бути числово не меншим за configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5), інакше FromISR-виклики недопустимі */
#define PLATFORM_IRQ_PRIO 6

/* ---------------- DWT і час ---------------- */
static uint32_t last_cyc, rem_cyc, us_total;

void platform_perf_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    last_cyc = DWT->CYCCNT;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5 | GPIO_PIN_6;
    g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
    GPIOB->BSRR = (GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5 | GPIO_PIN_6) << 16;
}

uint32_t platform_cycles(void) { return DWT->CYCCNT; }
uint32_t platform_cpu_hz(void) { return SystemCoreClock; }

uint32_t platform_micros(void)
{
    uint32_t now = DWT->CYCCNT, per_us = SystemCoreClock / 1000000U;
    rem_cyc += now - last_cyc;
    last_cyc = now;
    us_total += rem_cyc / per_us;
    rem_cyc %= per_us;
    return us_total;
}

void platform_probe(int ch, int level)
{
    static const uint16_t pin[PLATFORM_PROBE_COUNT] = { GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_3 };
    if (ch < 0 || ch >= PLATFORM_PROBE_COUNT) return;
    GPIOB->BSRR = level ? (uint32_t)pin[ch] : ((uint32_t)pin[ch] << 16);
}

/* ---------------- Стек і heap ---------------- */
extern uint32_t _estack;
extern uint32_t _Min_Stack_Size;
#define STACK_PATTERN 0xDEADBEEFU

static uint32_t *stack_bottom(void) { return (uint32_t *)((uint32_t)&_estack - (uint32_t)&_Min_Stack_Size); }

void platform_stack_paint(void)
{
    uint32_t *p = stack_bottom(), *sp = (uint32_t *)__get_MSP();
    while (p < sp - 16) *p++ = STACK_PATTERN;      /* поточний кадр не чіпаємо */
}

uint32_t platform_stack_used(void)
{
    uint32_t *p = stack_bottom();
    while (p < &_estack && *p == STACK_PATTERN) p++;
    return (uint32_t)&_estack - (uint32_t)p;
}

uint32_t platform_stack_reserved(void) { return (uint32_t)&_Min_Stack_Size; }
uint32_t platform_heap_used(void)      { struct mallinfo mi = mallinfo(); return (uint32_t)mi.arena; }

/* ---------------- Кнопка B1 (PC13) по EXTI ---------------- */
static volatile uint32_t btn_cyc;
static volatile uint8_t  btn_pending;
static volatile uint32_t btn_overrun;      /* подія прийшла, поки попередню ще не забрала задача */

static void (*btn_hook)(uint32_t cyc);       /* ЛР4: ISR -> ядро ОС (черга подій), замість прапорця */
static void (*rx_hook)(uint8_t b);           /* ЛР4: ISR -> stream buffer */
void platform_set_button_hook(void (*h)(uint32_t cyc)) { btn_hook = h; }
void platform_set_uart_hook(void (*h)(uint8_t b))      { rx_hook = h; }

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == BTN_PIN) {
        if (btn_hook) { btn_hook(DWT->CYCCNT); return; }
        if (btn_pending) btn_overrun++;
        else { btn_cyc = DWT->CYCCNT; btn_pending = 1; }       /* момент ПЕРШОЇ непрочитаної події */
    }
}

void platform_exti_irq(void) { HAL_GPIO_EXTI_IRQHandler(BTN_PIN); }

static void button_irq_enable(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = BTN_PIN; g.Mode = GPIO_MODE_IT_FALLING; g.Pull = GPIO_NOPULL;   /* на платі є зовнішній підтяг */
    HAL_GPIO_Init(BTN_PORT, &g);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, PLATFORM_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

uint32_t platform_button_overruns(void) { return btn_overrun; }

int platform_button_event(uint32_t *t_cycles)
{
    if (!btn_pending) return 0;
    *t_cycles = btn_cyc;
    btn_pending = 0;
    return 1;
}

/* ---------------- UART RX через IRQ ---------------- */
static volatile uint32_t eol_cyc;
static uint8_t rx_mem[256];
static ring_t  rx_ring;

void platform_uart_rx_irq_enable(void)
{
    ring_init(&rx_ring, rx_mem, sizeof rx_mem);
    button_irq_enable();
    USART2->CR1 |= USART_CR1_RXNEIE;
    HAL_NVIC_SetPriority(USART2_IRQn, PLATFORM_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void platform_uart2_irq(void)
{
    uint32_t sr = USART2->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t b = (uint8_t)USART2->DR;          /* читання DR знімає RXNE і ORE */
        if (sr & USART_SR_ORE) rx_ring.overflow++;            /* апаратний overrun: байт(и) втрачено, поки ISR не встигла */
        if (sr & USART_SR_RXNE) {
            if (b == '\r' || b == '\n') eol_cyc = DWT->CYCCNT;
            if (rx_hook) rx_hook(b); else ring_put(&rx_ring, b);
        }
    }
}

uint32_t platform_uart_rx_overflow(void) { return rx_ring.overflow; }
uint32_t platform_uart_eol_cycles(void)  { return eol_cyc; }
int platform_uart_getc(void) { return ring_get(&rx_ring); }

/* ---------------- Імітатор кнопки (ЛР4): TIM2 періодично ставить EXTI-подію кнопки програмно ---------------- */
/* Той самий ланцюжок, що й від справжнього натискання: EXTI13 -> EXTI15_10_IRQHandler -> HAL callback. Дає повторювані події без людини. */
void platform_btnsim(uint32_t period_ms)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1 = 0;
    if (period_ms == 0) { TIM2->DIER = 0; HAL_NVIC_DisableIRQ(TIM2_IRQn); return; }
    TIM2->PSC = (SystemCoreClock / 1000000U) - 1U;          /* TIM2 на APB1 x2 = SYSCLK: 1 МГц */
    TIM2->ARR = period_ms * 1000U - 1U;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM2_IRQn, PLATFORM_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 = TIM_CR1_CEN;
}

void platform_tim2_irq(void)
{
    TIM2->SR = 0;
    EXTI->SWIER = BTN_PIN;                                    /* програмна подія на лінії кнопки */
}

/* ---------------- Інжекція відмови (ЛР4): SDA I2C1 (PB9) замкнено на землю програмно ---------------- */
/* Те саме, що й дротом на GND (сценарій ЛР3 «замкнена SDA»), але повторюється командою: PB9 переводиться з AF у вихід open-drain низько. */
void platform_fault_sda(int on)
{
    if (on) { GPIOB->BSRR = (uint32_t)GPIO_PIN_9 << 16; GPIOB->MODER = (GPIOB->MODER & ~(3U << 18)) | (1U << 18); }
    else    { GPIOB->MODER = (GPIOB->MODER & ~(3U << 18)) | (2U << 18); }
}
