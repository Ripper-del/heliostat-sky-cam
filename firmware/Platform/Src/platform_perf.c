/** @file platform_perf.c — DWT, час у мкс, службові піни, стек/heap, кнопка (EXTI), UART RX/TX (DMA). */
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

static void (*btn_hook)(uint32_t isr_cyc, uint32_t evt_cyc);       /* ЛР4: ISR -> ядро ОС (черга подій), замість прапорця */
static volatile uint32_t sim_evt_cyc, sim_evt_valid;     /* момент справжньої події імітатора (див. platform_tim2_irq) */
static void (*rx_hook)(uint8_t b);           /* ЛР4: ISR -> stream buffer */
void platform_set_button_hook(void (*h)(uint32_t isr_cyc, uint32_t evt_cyc)) { btn_hook = h; }
void platform_set_uart_hook(void (*h)(uint8_t b))      { rx_hook = h; }

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == BTN_PIN) {
        uint32_t now = DWT->CYCCNT;
        if (btn_hook) { btn_hook(now, sim_evt_valid ? sim_evt_cyc : now); sim_evt_valid = 0; return; }
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

/* ---------------- UART2 через DMA (ЛР4) ----------------
 * Приймання: DMA1 Stream5 (канал 4) пише байти в кільцевий буфер rx_dma сам, без процесора, тож байти не губляться,
 * навіть коли переривання вимкнені (рядок камери, до 0,9 мс). ISR (IDLE на лінії, половина/кінець буфера DMA) лише
 * забирає нові байти з буфера і віддає їх у rx_hook (stream buffer задачі cli) або в rx_ring (без ОС).
 * Передавання: printf кладе байти в кільце tx_mem (4 КБ), DMA1 Stream6 (канал 4) відправляє його порціями.
 * Задача, що друкує, не чекає UART; якщо кільце заповнене — спить через platform_delay_ms (vTaskDelay), а не крутиться. */
#define RX_DMA_SIZE 256U
#define TX_SIZE     4096U
static volatile uint32_t eol_cyc;
static uint8_t rx_mem[256];
static ring_t  rx_ring;
static uint8_t rx_dma[RX_DMA_SIZE];
static uint32_t rx_pos;                       /* скільки байтів з rx_dma вже забрано */
static volatile uint32_t rx_dma_overrun;      /* DMA обійшло буфер, поки ISR не встигла (не мало б траплятись) */
static uint8_t tx_mem[TX_SIZE];
static volatile uint32_t tx_head, tx_tail;    /* head пише задача (під print_mx), tail рухає ISR DMA */
static volatile uint32_t tx_len;              /* довжина порції, яку зараз відправляє DMA (0 — DMA вільний) */
static int mirror_on;                         /* дзеркало на USART1 (PA9) для аналізатора: та сама порція через DMA2 Stream7 */

static void rx_drain(void)
{
    uint32_t pos = RX_DMA_SIZE - DMA1_Stream5->NDTR;            /* куди DMA запише наступний байт */
    if (pos == RX_DMA_SIZE) pos = 0;
    while (rx_pos != pos) {
        uint8_t b = rx_dma[rx_pos];
        rx_pos = (rx_pos + 1U) % RX_DMA_SIZE;
        if (b == '\r' || b == '\n') eol_cyc = DWT->CYCCNT;
        if (rx_hook) rx_hook(b); else ring_put(&rx_ring, b);
    }
}

static void tx_kick(void)                      /* викликається з вимкненими перериваннями */
{
    if (tx_len || tx_head == tx_tail) return;
    uint32_t t = tx_tail % TX_SIZE, n = tx_head - tx_tail;
    if (n > TX_SIZE - t) n = TX_SIZE - t;     /* до кінця кільця, решта — наступною порцією */
    tx_len = n;
    DMA1->HIFCR = DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CFEIF6;
    DMA1_Stream6->M0AR = (uint32_t)&tx_mem[t];
    DMA1_Stream6->NDTR = n;
    DMA1_Stream6->CR |= DMA_SxCR_EN;
    if (mirror_on && !(DMA2_Stream7->CR & DMA_SxCR_EN)) {
        DMA2->HIFCR = DMA_HIFCR_CTCIF7 | DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTEIF7 | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7;
        DMA2_Stream7->M0AR = (uint32_t)&tx_mem[t];
        DMA2_Stream7->NDTR = n;
        DMA2_Stream7->CR |= DMA_SxCR_EN;
    }
}

void platform_uart_rx_irq_enable(void)        /* назва з ЛР3; тепер вмикає DMA для обох напрямків */
{
    ring_init(&rx_ring, rx_mem, sizeof rx_mem);
    button_irq_enable();
    __HAL_RCC_DMA1_CLK_ENABLE();
    /* приймання */
    USART2->CR1 &= ~USART_CR1_RXNEIE;
    DMA1_Stream5->CR = 0; while (DMA1_Stream5->CR & DMA_SxCR_EN) { }
    DMA1->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5;
    DMA1_Stream5->PAR  = (uint32_t)&USART2->DR;
    DMA1_Stream5->M0AR = (uint32_t)rx_dma;
    DMA1_Stream5->NDTR = RX_DMA_SIZE;
    DMA1_Stream5->CR   = (4U << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_HTIE | DMA_SxCR_TCIE;
    DMA1_Stream5->CR  |= DMA_SxCR_EN;
    rx_pos = 0;
    (void)USART2->SR; (void)USART2->DR;
    USART2->CR3 |= USART_CR3_DMAR;
    USART2->CR1 |= USART_CR1_IDLEIE;
    /* передавання */
    DMA1_Stream6->CR = 0; while (DMA1_Stream6->CR & DMA_SxCR_EN) { }
    DMA1_Stream6->PAR = (uint32_t)&USART2->DR;
    DMA1_Stream6->CR  = (4U << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC | DMA_SxCR_DIR_0 | DMA_SxCR_TCIE;
    USART2->CR3 |= USART_CR3_DMAT;
    extern UART_HandleTypeDef huart1;
    if (huart1.gState == HAL_UART_STATE_READY) {
        __HAL_RCC_DMA2_CLK_ENABLE();
        DMA2_Stream7->CR = 0; while (DMA2_Stream7->CR & DMA_SxCR_EN) { }
        DMA2_Stream7->PAR = (uint32_t)&USART1->DR;
        DMA2_Stream7->CR  = (4U << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC | DMA_SxCR_DIR_0;
        USART1->CR3 |= USART_CR3_DMAT;
        mirror_on = 1;
    }
    HAL_NVIC_SetPriority(USART2_IRQn, PLATFORM_IRQ_PRIO, 0);
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, PLATFORM_IRQ_PRIO, 0);
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, PLATFORM_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    extern int uart_dma_ready;
    uart_dma_ready = 1;
}

void platform_uart2_irq(void)
{
    uint32_t sr = USART2->SR;
    if (sr & (USART_SR_IDLE | USART_SR_ORE)) {
        (void)USART2->DR;                       /* SR, потім DR — знімає IDLE/ORE */
        if (sr & USART_SR_ORE) rx_ring.overflow++;
    }
    rx_drain();
}

void platform_dma1_s5_irq(void)               /* приймання: половина або кінець кільця */
{
    uint32_t f = DMA1->HISR;
    DMA1->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5;
    if (f & DMA_HISR_TEIF5) rx_dma_overrun++;
    rx_drain();
}

void platform_dma1_s6_irq(void)               /* передавання: порцію відправлено, беремо наступну */
{
    DMA1->HIFCR = DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CFEIF6;
    if (mirror_on) { while (DMA2_Stream7->CR & DMA_SxCR_EN) { } }   /* USART1 з тією самою швидкістю закінчує одночасно */
    tx_tail += tx_len;
    tx_len = 0;
    tx_kick();
}

int platform_uart_write(const uint8_t *p, int len)
{
    int done = 0;
    while (done < len) {
        uint32_t room = TX_SIZE - (tx_head - tx_tail);
        if (room == 0) {                         /* кільце повне: чекаємо, поки DMA звільнить місце */
            if (__get_PRIMASK() || __get_BASEPRI()) {   /* переривання DMA замасковані (аварія або до старту планувальника): обслуговуємо самі */
                while (DMA1_Stream6->CR & DMA_SxCR_EN) { }
                platform_dma1_s6_irq();
            }
            else platform_delay_ms(1);
            continue;
        }
        uint32_t n = (uint32_t)(len - done);
        if (n > room) n = room;
        for (uint32_t i = 0; i < n; i++) tx_mem[(tx_head + i) % TX_SIZE] = p[done + (int)i];
        __DMB();
        tx_head += n;
        done += (int)n;
        uint32_t pm = __get_PRIMASK(); __disable_irq();
        tx_kick();
        if (!pm) __enable_irq();
    }
    return len;
}

/* Аварійний вивід (HardFault, переповнення стека): дочекатися поточної порції і відправити решту кільця опитуванням */
void platform_uart_flush_polling(void)
{
    while (DMA1_Stream6->CR & DMA_SxCR_EN) { }
    if (tx_len) { tx_tail += tx_len; tx_len = 0; }
    while (tx_head != tx_tail) {
        while (!(USART2->SR & USART_SR_TXE)) { }
        USART2->DR = tx_mem[tx_tail % TX_SIZE];
        tx_tail++;
    }
    while (!(USART2->SR & USART_SR_TC)) { }
}

uint32_t platform_uart_rx_overflow(void) { return rx_ring.overflow + rx_dma_overrun; }
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
    uint32_t cnt = TIM2->CNT, now = DWT->CYCCNT;               /* CNT = мкс від апаратного переповнення: скільки ISR чекала через вимкнені IRQ */
    TIM2->SR = 0;
    sim_evt_cyc = now - cnt * (SystemCoreClock / 1000000U);    /* справжній момент події в тактах DWT */
    sim_evt_valid = 1;
    EXTI->SWIER = BTN_PIN;                                    /* програмна подія на лінії кнопки */
}

/* ---------------- Інжекція відмови (ЛР4): SDA I2C1 (PB9) замкнено на землю програмно ---------------- */
/* Те саме, що й дротом на GND (сценарій ЛР3 «замкнена SDA»), але повторюється командою: PB9 переводиться з AF у вихід open-drain низько. */
void platform_fault_sda(int on)
{
    if (on) { GPIOB->BSRR = (uint32_t)GPIO_PIN_9 << 16; GPIOB->MODER = (GPIOB->MODER & ~(3U << 18)) | (1U << 18); }
    else    { GPIOB->MODER = (GPIOB->MODER & ~(3U << 18)) | (2U << 18); }
}
