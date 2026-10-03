/* Includes ------------------------------------------------------------------*/
#include "bsp.h"
#include "stm32h5xx_it.h"
#include "hw_def.h"
#include "fault.h"


/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  while (1)
  {
  }
}

/**
  * @brief This function handles Hard fault interrupt.
  */
__attribute__((naked)) void HardFault_Handler(void)
{
  // 예외가 스택 프레임을 쌓은 쪽(EXC_RETURN bit2: 0 = MSP, 1 = PSP)을 R0 로 넘긴다
  __asm volatile
  (
    "tst   lr, #4             \n"
    "ite   eq                 \n"
    "mrseq r0, msp            \n"
    "mrsne r0, psp            \n"
    "b     HardFault_Handler_C   \n"
  );
}

void HardFault_Handler_C(uint32_t *p_stack)
{
#ifdef _USE_HW_FAULT
  faultReset("HardFault", p_stack);
#endif
  while (1)
  {
  }
}

/**
  * @brief This function handles Memory management fault.
  */
__attribute__((naked)) void MemManage_Handler(void)
{
  // 예외가 스택 프레임을 쌓은 쪽(EXC_RETURN bit2: 0 = MSP, 1 = PSP)을 R0 로 넘긴다
  __asm volatile
  (
    "tst   lr, #4             \n"
    "ite   eq                 \n"
    "mrseq r0, msp            \n"
    "mrsne r0, psp            \n"
    "b     MemManage_Handler_C   \n"
  );
}

void MemManage_Handler_C(uint32_t *p_stack)
{
#ifdef _USE_HW_FAULT
  faultReset("MemManage", p_stack);
#endif
  while (1)
  {
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
__attribute__((naked)) void BusFault_Handler(void)
{
  // 예외가 스택 프레임을 쌓은 쪽(EXC_RETURN bit2: 0 = MSP, 1 = PSP)을 R0 로 넘긴다
  __asm volatile
  (
    "tst   lr, #4             \n"
    "ite   eq                 \n"
    "mrseq r0, msp            \n"
    "mrsne r0, psp            \n"
    "b     BusFault_Handler_C   \n"
  );
}

void BusFault_Handler_C(uint32_t *p_stack)
{
#ifdef _USE_HW_FAULT
  faultReset("BusFault", p_stack);
#endif
  while (1)
  {
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
__attribute__((naked)) void UsageFault_Handler(void)
{
  // 예외가 스택 프레임을 쌓은 쪽(EXC_RETURN bit2: 0 = MSP, 1 = PSP)을 R0 로 넘긴다
  __asm volatile
  (
    "tst   lr, #4             \n"
    "ite   eq                 \n"
    "mrseq r0, msp            \n"
    "mrsne r0, psp            \n"
    "b     UsageFault_Handler_C   \n"
  );
}

void UsageFault_Handler_C(uint32_t *p_stack)
{
#ifdef _USE_HW_FAULT
  faultReset("UsageFault", p_stack);
#endif
  while (1)
  {
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
}
