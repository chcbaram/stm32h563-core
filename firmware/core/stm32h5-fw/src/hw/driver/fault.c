#include "fault.h"
#include "reset.h"
#include "log.h"
#include "cli.h"


#ifdef _USE_HW_FAULT


/* 폴트가 나면 레지스터를 .noinit(SRAM, 리셋에도 남는다) 에 적고 리셋한다. 다음 부팅의 faultInit() 이 찍는다.

   핸들러 진입부(stm32h5xx_it.c 의 naked 함수)가 예외가 쌓은 스택 프레임 주소를 넘긴다.
     p_stack[0..7] = R0 R1 R2 R3 R12 LR PC xPSR
   PC 는 폴트를 낸 명령, LR 은 그 함수를 부른 곳이다.

   w6300 fault.c 를 바탕으로 CFSR / HFSR / MMFAR / BFAR 를 더했다. PC 만으로는 왜 났는지 모른다. */
#define FAULT_MAGIC           0x5555AAAA


#if CLI_USE(HW_FAULT)
static void cliFault(cli_args_t *args);
#endif

static void faultPrint(fault_log_t *p_log);


static __attribute__((section(".noinit"))) fault_log_t fault_log;

//-- 이번 부팅이 폴트 직후인가.
//   fault_log 는 .noinit 이라 오래전 폴트의 값도 남는다. 전원을 막 넣은 직후라면 쓰레기값이다.
//   faultInit() 이 매직을 보고 한 번만 걷어 담고 매직을 지운다.
static bool        is_fault_boot = false;
static fault_log_t fault_boot_log;




bool faultInit(void)
{
  if (fault_log.magic_number == FAULT_MAGIC)
  {
    fault_log.magic_number = 0;
    fault_boot_log = fault_log;
    is_fault_boot  = true;

    logPrintf("[!!] Fault Boot\n");
    faultPrint(&fault_boot_log);
  }
  logPrintf("[OK] faultInit()\n");

#if CLI_USE(HW_FAULT)
  cliAdd("fault", cliFault);
#endif
  return true;
}

void faultReset(const char *p_msg, uint32_t *p_stack)
{
  fault_log.magic_number = FAULT_MAGIC;

  if (p_stack != NULL)
  {
    fault_log.is_reg  = true;
    fault_log.REG_R0  = p_stack[0];
    fault_log.REG_R1  = p_stack[1];
    fault_log.REG_R2  = p_stack[2];
    fault_log.REG_R3  = p_stack[3];
    fault_log.REG_R12 = p_stack[4];
    fault_log.REG_LR  = p_stack[5];
    fault_log.REG_PC  = p_stack[6];
    fault_log.REG_PSR = p_stack[7];
  }
  else
  {
    fault_log.is_reg = false;
  }
  fault_log.REG_CFSR  = SCB->CFSR;
  fault_log.REG_HFSR  = SCB->HFSR;
  fault_log.REG_MMFAR = SCB->MMFAR;
  fault_log.REG_BFAR  = SCB->BFAR;

  strncpy(fault_log.msg, p_msg, sizeof(fault_log.msg) - 1);
  fault_log.msg[sizeof(fault_log.msg) - 1] = 0;

  // 폴트로 인한 리셋 횟수를 백업 레지스터에 누적한다 (.noinit 은 전원이 끊기면 사라진다)
  resetIncFaultCount();

  NVIC_SystemReset();
}

bool faultGetLog(fault_log_t *p_log)
{
  if (p_log == NULL)
    return false;

  *p_log = fault_boot_log;
  return is_fault_boot;
}

bool faultIsFaultBoot(void)
{
  return is_fault_boot;
}

// 폴트 직후 부팅이 아니면 0
uint32_t faultGetPc(void)
{
  return (is_fault_boot && fault_boot_log.is_reg) ? fault_boot_log.REG_PC : 0;
}

void faultPrint(fault_log_t *p_log)
{
  uint32_t cfsr = p_log->REG_CFSR;

  logPrintf("     Msg   : %s\n", p_log->msg);
  if (p_log->is_reg == true)
  {
    logPrintf("     PC    : 0x%08X\n", (unsigned int)p_log->REG_PC);
    logPrintf("     LR    : 0x%08X\n", (unsigned int)p_log->REG_LR);
    logPrintf("     PSR   : 0x%08X\n", (unsigned int)p_log->REG_PSR);
    logPrintf("     R0~R3 : 0x%08X 0x%08X 0x%08X 0x%08X\n",
              (unsigned int)p_log->REG_R0, (unsigned int)p_log->REG_R1,
              (unsigned int)p_log->REG_R2, (unsigned int)p_log->REG_R3);
    logPrintf("     R12   : 0x%08X\n", (unsigned int)p_log->REG_R12);
  }
  logPrintf("     CFSR  : 0x%08X\n", (unsigned int)cfsr);
  logPrintf("     HFSR  : 0x%08X%s\n", (unsigned int)p_log->REG_HFSR,
            (p_log->REG_HFSR & SCB_HFSR_FORCED_Msk) ? " (FORCED)" : "");

  // 원인 비트 중 자주 보는 것만 풀어 쓴다 (전체 정의는 ARMv8-M ARM / PM0264)
  if (cfsr & SCB_CFSR_IACCVIOL_Msk)   logPrintf("       MM  : instruction access violation\n");
  if (cfsr & SCB_CFSR_DACCVIOL_Msk)   logPrintf("       MM  : data access violation\n");
  if (cfsr & SCB_CFSR_MMARVALID_Msk)  logPrintf("       MM  : MMFAR 0x%08X\n", (unsigned int)p_log->REG_MMFAR);
  if (cfsr & SCB_CFSR_IBUSERR_Msk)    logPrintf("       BUS : instruction bus error\n");
  if (cfsr & SCB_CFSR_PRECISERR_Msk)  logPrintf("       BUS : precise data bus error\n");
  if (cfsr & SCB_CFSR_IMPRECISERR_Msk)logPrintf("       BUS : imprecise data bus error\n");
  if (cfsr & SCB_CFSR_BFARVALID_Msk)  logPrintf("       BUS : BFAR 0x%08X\n", (unsigned int)p_log->REG_BFAR);
  if (cfsr & SCB_CFSR_UNDEFINSTR_Msk) logPrintf("       USG : undefined instruction\n");
  if (cfsr & SCB_CFSR_INVSTATE_Msk)   logPrintf("       USG : invalid state (thumb bit)\n");
  if (cfsr & SCB_CFSR_INVPC_Msk)      logPrintf("       USG : invalid PC load\n");
  if (cfsr & SCB_CFSR_NOCP_Msk)       logPrintf("       USG : no coprocessor\n");
  if (cfsr & SCB_CFSR_STKOF_Msk)      logPrintf("       USG : stack overflow\n");
  if (cfsr & SCB_CFSR_UNALIGNED_Msk)  logPrintf("       USG : unaligned access\n");
  if (cfsr & SCB_CFSR_DIVBYZERO_Msk)  logPrintf("       USG : divide by zero\n");
}


#if CLI_USE(HW_FAULT)
void cliFault(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("fault boot  : %s\n", is_fault_boot ? "yes" : "no");
    cliPrintf("fault count : %d\n", (int)resetGetFaultCount());
    if (is_fault_boot)
    {
      faultPrint(&fault_boot_log);
    }
    ret = true;
  }

  // 일부러 폴트를 낸다. 리셋된 뒤 부팅 로그에 기록이 찍혀야 한다.
  if (args->argc == 2 && args->isStr(0, "test"))
  {
    if (args->isStr(1, "bus"))
    {
      // OCTOSPI 창(0x9000_0000~)에서 칩 크기(32 MB) 밖. 메모리 맵이 꺼져 있으면 창 전체가 버스 오류다.
      // (FMC 영역 0x6000_0000 은 FMC 를 켜지 않아도 오류가 나지 않았다)
      volatile uint32_t data = *(volatile uint32_t *)0x9F000000;
      (void)data;
    }
    if (args->isStr(1, "udf"))
    {
      __asm volatile ("udf #0");
    }
    if (args->isStr(1, "div"))
    {
      volatile int a = 1;
      volatile int b = 0;
      SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
      a = a / b;
    }
    cliPrintf("no fault?\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("fault info\n");
    cliPrintf("fault test bus|udf|div\n");
  }
}
#endif

#endif
