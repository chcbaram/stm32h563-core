#include "hw.h"



extern uint32_t _fw_flash_begin;
extern uint32_t _fw_flash_size;

volatile const firm_ver_t firm_ver __attribute__((section(".version"))) = 
{
  .magic_number = VERSION_MAGIC_NUMBER,
  .version_str  = _DEF_FIRMWATRE_VERSION,
  .name_str     = _DEF_BOARD_NAME,
  .firm_addr    = (uint32_t)&_fw_flash_begin,
  .firm_size    = (uint32_t)&_fw_flash_size,    // 링커 심볼 값 (C 에서 end - begin 을 계산하면 0 으로 접힌다, N6/weact)
};



bool hwInit(void)
{  
  cliInit();
  logInit();
  ledInit();
  uartInit();
  for (int i=0; i<HW_UART_MAX_CH; i++)
  {
    uartOpen(i, 115200);
  }

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);  
  logPrintf("Booting..Clock\t\t: %d Mhz\r\n", (int)HAL_RCC_GetSysClockFreq()/1000000);
  logPrintf("Booting..Date \t\t: %s\r\n", __DATE__); 
  logPrintf("Booting..Time \t\t: %s\r\n", __TIME__); 
  logPrintf("Booting..Addr \t\t: 0x%X\r\n", (uint32_t)&_fw_flash_begin); 
  logPrintf("\n");

  rtcInit();
  resetInit();
  faultInit();
  flashInit();
  qspiInit();
  
  return true;
}
