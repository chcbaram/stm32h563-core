#ifndef __STM32H562_IT_H
#define __STM32H562_IT_H

#ifdef __cplusplus
 extern "C" {
#endif


void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void HardFault_Handler_C(uint32_t *p_stack);
void MemManage_Handler_C(uint32_t *p_stack);
void BusFault_Handler_C(uint32_t *p_stack);
void UsageFault_Handler_C(uint32_t *p_stack);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);


#ifdef __cplusplus
}
#endif

#endif 
