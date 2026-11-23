/** @file stm32f4xx_it.c — обробники виключень Cortex-M4. */
#include "main.h"
#include "stm32f4xx_it.h"
#include "platform.h"

void NMI_Handler(void)        { while (1) {} }
void HardFault_Handler(void)  { while (1) {} }
void MemManage_Handler(void)  { while (1) {} }
void BusFault_Handler(void)   { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void SVC_Handler(void)        {}
void DebugMon_Handler(void)   {}
void PendSV_Handler(void)     {}

void EXTI15_10_IRQHandler(void) { platform_exti_irq(); }
void USART2_IRQHandler(void)    { platform_uart2_irq(); }

/* SysTick іде щомілісекунди й збільшує лічильник HAL_GetTick(). */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
