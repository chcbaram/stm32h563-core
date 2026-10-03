#include "module.h"


/* CLI 포트

     기본          : UART (ST-LINK VCP)
     USB CDC 를    : 호스트가 115200 으로 열면 터미널로 보고 CLI 를 CDC 로 옮긴다. 로그도 따라간다.
                     그 외 보율이면 호스트 툴(cmd)의 것이다 (cmd_task.c 의 cmdChIsEnabled()).
     CDC 가 닫히면  : UART 로 돌아온다

   CDC 를 uart 채널(HW_UART_CH_USB)로 꽂아 두어 cli / log 는 uart 함수만 쓴다 (cdc.c 의 cdc_uart_driver). */


static bool cliModuleInit(void);
static void cliModuleUpdate(void const *arg);

static uint32_t cli_baud = 115200;


MODULE_DEF(cli)
{
  .name     = "cli",
  .priority = MODULE_PRI_NORMAL,
  .init     = cliModuleInit,
  .update   = cliModuleUpdate,
};




bool cliModuleInit(void)
{
#ifdef _USE_HW_CDC
  uartSetDriver(HW_UART_CH_USB, &cdc_uart_driver);
  uartOpen(HW_UART_CH_USB, cli_baud);
#endif
  return cliOpen(HW_UART_CH_CLI, cli_baud);
}

void cliModuleUpdate(void const *arg)
{
  (void)arg;

  cliMain();

#ifdef _USE_HW_CDC
  uint8_t ch = HW_UART_CH_CLI;

  if (cdcIsConnect() == true && usbGetType() == USB_CON_CLI)
  {
    ch = HW_UART_CH_USB;
  }

  if (cliGetPort() != ch)
  {
    cliOpen(ch, cli_baud);
    logOpen(ch, cli_baud);
  }
#endif
}
