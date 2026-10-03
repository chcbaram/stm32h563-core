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
#define      HW_RESET_DBLCLK_MS     300
#define      HW_RESET_DBLCLK_CNT    2

#define _USE_HW_FLASH


//-- Flash Layout
//   부트로더(stm32h5-boot)를 붙이면 그쪽 hw_def.h 와 반드시 같게 유지할 것 (w6300 과 같은 주소)
//
//   0x0800_0000  BOOT   128 KB   부트로더 자리 (지금은 이 앱이 0x0800_0000 부터 돈다)
//   0x0802_0000  TAG      1 KB   firm_tag_t
//   0x0802_0400  APP   ~447 KB   앱 벡터, +0x400 에 firm_ver_t
//
#define FLASH_SIZE_TAG              0x400
#define FLASH_SIZE_VEC              0x400
#define FLASH_SIZE_VER              0x400

#define FLASH_ADDR_BOOT             0x08000000
#define FLASH_SIZE_BOOT             (128*1024)
#define FLASH_ADDR_FIRM             0x08020000
#define FLASH_SIZE_FIRM             (448*1024)
#define FLASH_ADDR_FIRM_VEC         (FLASH_ADDR_FIRM + FLASH_SIZE_TAG)

//   앱은 실행 중인 뱅크1 전체를 보호한다. 기록은 뱅크2 에만 한다 (w6300 과 같다).
#define FLASH_PROTECT_ADDR          0x08000000
#define FLASH_PROTECT_SIZE          (1024*1024)


//-- CLI
//
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_UART            1
#define _USE_CLI_HW_RTC             1
#define _USE_CLI_HW_RESET           1
#define _USE_CLI_HW_FLASH           1


#endif
