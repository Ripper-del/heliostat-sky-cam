/** @file stm32f4xx_it.c — обробники виключень Cortex-M4. */
#include "main.h"
#include "stm32f4xx_it.h"
#include "platform.h"
#include "FreeRTOS.h"
#include "task.h"
#include "rtos_port.h"
void xPortSysTickHandler(void);

void NMI_Handler(void)        { while (1) {} }
void HardFault_Handler(void)  { while (1) {} }
void MemManage_Handler(void)  { while (1) {} }
void BusFault_Handler(void)   { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void DebugMon_Handler(void)   {}

void EXTI15_10_IRQHandler(void) { platform_exti_irq(); }
void USART2_IRQHandler(void)    { platform_uart2_irq(); }
void TIM2_IRQHandler(void)      { platform_tim2_irq(); }     /* імітатор кнопки (ЛР4) */

/* SysTick: і лічильник HAL_GetTick(), і тік FreeRTOS. Час обробника міряємо (ЛР4, п. 6.5). SVC_Handler і PendSV_Handler — з порту ядра (FreeRTOSConfig.h). */
void SysTick_Handler(void)
{
    uint32_t t0 = DWT->CYCCNT;
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) xPortSysTickHandler();
    rtos_tick_cost(DWT->CYCCNT - t0);
}
