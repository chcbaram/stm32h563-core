#ifndef HW_DEF_H_
#define HW_DEF_H_



#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V261003R1"
#define _DEF_BOARD_NAME           "STM32H5-BOOT"

#define _USE_HW_FAULT
#define _USE_HW_FLASH
#define _USE_HW_QSPI


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
//   백업 레지스터 배정. 앱(stm32h5-fw) hw_def.h 와 반드시 같아야 한다 (w6300 과 같은 배정).
#define      HW_RTC_BOOT_MODE       RTC_BKP_DR3
#define      HW_RTC_RESET_BITS      RTC_BKP_DR4
#define      HW_RTC_RESET_CNT       RTC_BKP_DR5
#define      HW_RTC_BOOT_TRY        RTC_BKP_DR6
#define      HW_RTC_FAULT_CNT       RTC_BKP_DR7
#define      HW_RTC_ECC_ADDR        RTC_BKP_DR8

#define _USE_HW_RESET
//   1 = 부트로더. 리셋 원인 플래그를 읽고 지운 뒤 백업 레지스터에 남긴다 (앱은 그것을 읽는다).
#define      HW_RESET_BOOT          1
#define      HW_RESET_DBLCLK_MS     300
#define      HW_RESET_DBLCLK_CNT    2

#define _USE_HW_CMD
#define      HW_CMD_MAX_DATA_LENGTH 1024
#define      HW_CMD_UART_CH         HW_UART_CH_SWD   // CLI 와 같은 포트. cli 의 RX 필터로 가른다


//-- Flash Layout
//   앱(stm32h5-fw) hw_def.h 와 반드시 같게 유지할 것
//
//   0x0800_0000  BOOT   256 KB   이 부트로더
//   0x0804_0000  TAG      1 KB   firm_tag_t
//   0x0804_0400  APP   ~447 KB   앱 벡터, +0x400 에 firm_ver_t
//
#define FLASH_SIZE_TAG              0x400
#define FLASH_SIZE_VEC              0x400
#define FLASH_SIZE_VER              0x400

#define FLASH_ADDR_BOOT             0x08000000
#define FLASH_SIZE_BOOT             (256*1024)
#define FLASH_ADDR_FIRM             0x08040000
#define FLASH_SIZE_FIRM             (448*1024)
#define FLASH_ADDR_FIRM_VEC         (FLASH_ADDR_FIRM + FLASH_SIZE_TAG)

//   부트로더는 자기 자신만 보호한다. FIRM 은 써야 한다.
#define FLASH_PROTECT_ADDR          FLASH_ADDR_BOOT
#define FLASH_PROTECT_SIZE          FLASH_SIZE_BOOT

#define HW_DEV_MODE                 HW_DEV_MODE_BOOT


//-- CLI
//
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_UART            1
#define _USE_CLI_HW_RTC             1
#define _USE_CLI_HW_RESET           1
#define _USE_CLI_HW_FLASH           1
#define _USE_CLI_HW_QSPI            1
#define _USE_CLI_HW_FAULT           1
#define _USE_CLI_HW_MODULE          1
#define _USE_CLI_HW_BOOT            1


#endif
