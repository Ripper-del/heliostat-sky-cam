/**
 * @file rtos_port.c — склейка FreeRTOS з рештою прошивки: хуки діагностики, серіалізація printf, облік перемикань,
 * вимір вартості тіка. Усе, що торкається ядра і не є логікою продукту.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "platform.h"
#include "rtstats.h"
#include "rtos_port.h"

extern void * volatile pxCurrentTCB;          /* tasks.c: задача, що щойно отримала процесор */

/* ---- аварійний вивід без printf (стек і heap можуть бути пошкоджені) ---- */
static void raw_puts(const char *s)
{
    uint32_t pm = __get_PRIMASK(); __disable_irq();
    for (; *s; s++) { while (!(USART2->SR & USART_SR_TXE)) { } USART2->DR = (uint8_t)*s; }
    while (!(USART2->SR & USART_SR_TC)) { }
    if (!pm) __enable_irq();
}

static volatile uint32_t os_fault_count;
uint32_t rtos_fault_count(void) { return os_fault_count; }

void rtos_assert_failed(const char *file, int line)
{
    __disable_irq();
    char b[64]; snprintf(b, sizeof b, "\r\nFATAL configASSERT %s:%d\r\n", file, line);
    raw_puts(b);
    for (;;) { }
}

/* Збій ядра процесора (HardFault): повідомлення в UART без printf і м'який перезапуск, щоб плата не висіла мовчки */
void rtos_fatal(const char *what)
{
    __disable_irq();
    raw_puts("\r\nFATAL ");
    raw_puts(what);
    raw_puts("\r\n");
    for (volatile uint32_t i = 0; i < 2000000; i++) { }
    NVIC_SystemReset();
}

/* Heap ОС вичерпано: повідомляємо і повертаємо NULL (pvPortMalloc сам поверне 0) — прошивка вирішує, що робити далі */
static volatile uint32_t malloc_failed;
uint32_t rtos_malloc_failed_count(void) { return malloc_failed; }
void vApplicationMallocFailedHook(void)
{
    malloc_failed++;
    raw_puts("\r\n[os] malloc failed hook\r\n");
}

/* Переповнення стека задачі: пошкодження вже сталось, продовжувати небезпечно — повідомлення і м'який перезапуск */
void vApplicationStackOverflowHook(TaskHandle_t t, char *name)
{
    (void)t;
    __disable_irq();
    os_fault_count++;
    raw_puts("\r\nFATAL stack overflow in ");
    raw_puts(name);
    raw_puts("\r\n");
    for (volatile uint32_t i = 0; i < 2000000; i++) { }      /* дати UART дотягнути */
    NVIC_SystemReset();
}

/* ---- printf з кількох задач: один м'ютекс на виклик ---- */
static SemaphoreHandle_t print_mx;
void rtos_print_init(void) { print_mx = xSemaphoreCreateMutex(); configASSERT(print_mx); }
static int locked_begin(void)
{
    if (print_mx && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) { xSemaphoreTake(print_mx, portMAX_DELAY); return 1; }
    return 0;
}
static void locked_end(int l) { if (l) xSemaphoreGive(print_mx); }

int __wrap_printf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int l = locked_begin();
    int n = vprintf(fmt, ap);
    locked_end(l);
    va_end(ap);
    return n;
}
extern int __real_puts(const char *s);
int __wrap_puts(const char *s) { int l = locked_begin(); int n = __real_puts(s); locked_end(l); return n; }
int __wrap_putchar(int c) { int l = locked_begin(); int n = putchar(c); locked_end(l); return n; }

/* ---- облік перемикань контексту (traceTASK_SWITCHED_IN з FreeRTOSConfig.h) ---- */
rtstats_t g_rts;
static volatile uint32_t idle_tcb;
static volatile int idle_pin_enabled = 1;

uint32_t rtos_runtime_cycles(void) { return DWT->CYCCNT; }

static volatile uint32_t sw_gap_max, sw_gap_task, sw_last;
void rtos_switch_gap(uint32_t *gap_us, uint32_t *task) { *gap_us = sw_gap_max / (SystemCoreClock / 1000000U); *task = sw_gap_task; }
void rtos_switch_gap_reset(void) { sw_gap_max = 0; }

void rtos_trace_switch_in(void)
{
    uint32_t id = (uint32_t)pxCurrentTCB;
    uint32_t nowc = DWT->CYCCNT;
    if (sw_last) { uint32_t g = nowc - sw_last; if (g > sw_gap_max && g_rts.cur_id != idle_tcb) { sw_gap_max = g; sw_gap_task = g_rts.cur_id; } }   /* найдовше безперервне виконання однієї не-idle задачі і хто це */
    sw_last = nowc;
    if (!idle_tcb) idle_tcb = (uint32_t)xTaskGetIdleTaskHandle();     /* idle створюється при старті планувальника, до першого перемикання */
    rts_switch(&g_rts, id, DWT->CYCCNT);
    if (idle_pin_enabled) platform_probe(1, id == idle_tcb);        /* PB2 = 1, коли працює idle: вільний процесор видно на аналізаторі */
}

/* ---- вартість тіка: SysTick_Handler міряє свій час і додає сюди ---- */
static volatile uint32_t tick_n, tick_max; static volatile uint64_t tick_sum;
static volatile uint32_t tick_gap_max, tick_gaps, tick_last;
static volatile uint32_t isr_n, isr_to_kernel;         /* isr_n — кількість входів у SysTick; isr_n - kernel_tick = isr_to_kernel, коли відкладених тіків немає */
static volatile uint32_t tick_ts[128];                 /* ідеальний момент (DWT) кожного входу в SysTick: за ним міряється джитер періодичних задач */
void rtos_tick_cost(uint32_t cyc)
{
    uint32_t now = DWT->CYCCNT;
    uint32_t since_reload = SysTick->LOAD - SysTick->VAL;            /* скільки тактів минуло від справжнього моменту тіка до входу в ISR (IRQ-off, вищі ISR) */
    isr_n++;
    tick_ts[isr_n & 127U] = now - since_reload;
    /* Коли планувальник не призупинено, лічильник тіків ядра дорівнює isr_n мінус стала. Під час vTaskSuspendAll тіки відкладаються
     * (xPendedTicks) і лічильник відстає, тож сталу оновлюємо лише коли планувальник працює. */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) isr_to_kernel = isr_n - xTaskGetTickCount();
    if (tick_last) { uint32_t gap = (uint32_t)(now - tick_last); if (gap > tick_gap_max) tick_gap_max = gap; if (gap > SystemCoreClock / 1000U * 3U / 2U) tick_gaps++; }
    tick_last = now;
    tick_n++; tick_sum += cyc; if (cyc > tick_max) tick_max = cyc;
}
void rtos_tick_gap(uint32_t *gap_max_us, uint32_t *late_gaps) { *gap_max_us = tick_gap_max / (SystemCoreClock / 1000000U); *late_gaps = tick_gaps; }
void rtos_tick_gap_reset(void) { tick_gap_max = 0; tick_gaps = 0; }
void rtos_tick_stats(uint32_t *n, uint32_t *avg, uint32_t *max)
{
    taskENTER_CRITICAL();
    *n = tick_n; *avg = tick_n ? (uint32_t)(tick_sum / tick_n) : 0; *max = tick_max;
    taskEXIT_CRITICAL();
}
void rtos_tick_reset(void) { taskENTER_CRITICAL(); tick_n = 0; tick_sum = 0; tick_max = 0; taskEXIT_CRITICAL(); }

/* Ідеальний момент тіка ядра `tick` у тактах DWT (за входом у SysTick, де враховано затримку ISR); якщо запис застарів (> 120 мс) — оцінка від поточного моменту */
uint32_t rtos_tick_time(uint32_t tick)
{
    uint32_t idx = tick + isr_to_kernel, cyc = DWT->CYCCNT, age = isr_n - idx;
    if (age < 120U) return tick_ts[idx & 127U];
    return cyc - age * (SystemCoreClock / 1000U);
}
