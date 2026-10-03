#ifndef HW_DEF_H_
#define HW_DEF_H_



#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V261003R1"
#define _DEF_BOARD_NAME           "STM32H5-FW"



#define _USE_HW_LED
#define      HW_LED_MAX_CH          1

#define _USE_HW_UART
#define      HW_UART_MAX_CH         1
#define      HW_UART_CH_SWD         _DEF_UART1    // 디버그 커넥터 J1 (ST-LINK VCP)
#define      HW_UART_CH_CLI         HW_UART_CH_SWD

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_SWD
#define      HW_LOG_BOOT_BUF_MAX    2048
#define      HW_LOG_LIST_BUF_MAX    4096


#define _USE_HW_RTC
//   백업 레지스터 배정. 부트로더를 붙이면 부트로더 hw_def.h 와 반드시 같아야 한다 (w6300 과 같은 배정).
#define      HW_RTC_BOOT_MODE       RTC_BKP_DR3
#define      HW_RTC_RESET_BITS      RTC_BKP_DR4
#define      HW_RTC_RESET_CNT       RTC_BKP_DR5
#define      HW_RTC_BOOT_TRY        RTC_BKP_DR6
#define      HW_RTC_FAULT_CNT       RTC_BKP_DR7
#define      HW_RTC_ECC_ADDR        RTC_BKP_DR8

#define _USE_HW_RESET
//   1 = 리셋 원인 플래그를 직접 읽고 지운다. 지금은 부트로더가 없어 앱이 맡는다.
//   부트로더를 붙이면 0 으로 바꾼다 (부트로더가 읽고 백업 레지스터에 남긴 것을 앱이 읽는다).
#define      HW_RESET_BOOT          1
#define      HW_RESET_CNT_MAGIC     0xA55A0000UL
#define      HW_RESET_CNT_MASK      0x000000FFUL
#define      HW_RESET_DBLCLK_MS     300
#define      HW_RESET_DBLCLK_CNT    2
#define      HW_ECC_MAGIC           0xEC000000UL


//-- CLI
//
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_UART            1
#define _USE_CLI_HW_RTC             1
#define _USE_CLI_HW_RESET           1


#endif
