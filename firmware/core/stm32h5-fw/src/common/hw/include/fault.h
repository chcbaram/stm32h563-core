#ifndef FAULT_H_
#define FAULT_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_FAULT


typedef struct
{
  uint32_t magic_number;
  bool     is_reg;
  uint32_t REG_R0;
  uint32_t REG_R1;
  uint32_t REG_R2;
  uint32_t REG_R3;
  uint32_t REG_R12;
  uint32_t REG_LR;
  uint32_t REG_PC;
  uint32_t REG_PSR;
  uint32_t REG_CFSR;          // 폴트 원인 (MMFSR | BFSR | UFSR)
  uint32_t REG_HFSR;
  uint32_t REG_MMFAR;
  uint32_t REG_BFAR;
  char     msg[32];
} fault_log_t;


bool     faultInit(void);
void     faultReset(const char *p_msg, uint32_t *p_stack);
bool     faultGetLog(fault_log_t *p_log);
bool     faultIsFaultBoot(void);
uint32_t faultGetPc(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
