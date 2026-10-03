#include "cmd_task.h"
#include "module.h"


#ifdef _USE_HW_CMD
#include "driver/drv_uart.h"
#include "driver/drv_usb.h"
#include "driver/drv_hid.h"
#include "process/cmd_boot.h"


/* 채널마다 cmd_t 와 cmd_driver_t 를 하나씩 둔다 (패킷 파서 상태가 채널별로 독립이어야 한다).

     UART (ST-LINK VCP) : CLI 와 같은 포트. cli 의 RX 필터가 cmd 패킷만 골라낸다 (drv_uart.c)
     USB CDC            : 호스트가 연 보율로 CLI 와 주인을 가른다 (115200 = CLI, cmdChIsEnabled())
     USB HID            : cmd 전용. 언제나 동작한다

   N6 의 UART 하나짜리 구조에 w6300 의 CDC / HID 채널을 더했다. */
enum
{
  CMD_CH_UART = 0,
#ifdef _USE_HW_CDC
  CMD_CH_CDC,
#endif
#ifdef _USE_HW_USB
  CMD_CH_HID,
#endif
  CMD_CH_MAX
};

static const char *cmd_ch_name[CMD_CH_MAX] =
{
  "UART",
#ifdef _USE_HW_CDC
  "USB CDC",
#endif
#ifdef _USE_HW_USB
  "USB HID",
#endif
};


static void cmdTaskThread(void const *arg);
static bool cmdChIsEnabled(uint32_t ch);

static cmd_t        cmd[CMD_CH_MAX];
static cmd_driver_t cmd_driver[CMD_CH_MAX];


// cli 보다 늦게 연다. drvUartInit() 이 cli 에 RX 필터를 건다
MODULE_DEF(cmd)
{
  .name     = "cmd",
  .priority = MODULE_PRI_LOWEST,
  .init     = cmdTaskInit,
  .update   = cmdTaskThread,
};




bool cmdTaskInit(void)
{
  drvUartInit(&cmd_driver[CMD_CH_UART], HW_CMD_UART_CH, 115200);
#ifdef _USE_HW_CDC
  drvUsbInit(&cmd_driver[CMD_CH_CDC]);
#endif
#ifdef _USE_HW_USB
  drvHidInit(&cmd_driver[CMD_CH_HID]);
#endif

  for (uint32_t i = 0; i < CMD_CH_MAX; i++)
  {
    cmdInit(&cmd[i], &cmd_driver[i]);
    cmdOpen(&cmd[i]);
  }

  logPrintf("[OK] cmdTaskInit()\n");
  for (uint32_t i = 0; i < CMD_CH_MAX; i++)
  {
    logPrintf("     %s\n", cmd_ch_name[i]);
  }
  return true;
}

bool cmdTaskUpdate(void)
{
  drvUartUpdate(&cmd_driver[CMD_CH_UART]);

  for (uint32_t i = 0; i < CMD_CH_MAX; i++)
  {
    if (cmdChIsEnabled(i) != true)
      continue;

    if (cmdReceivePacket(&cmd[i]) == true)
    {
      cmdBootProcess(&cmd[i]);
    }
  }
  return true;
}

// CDC 는 CLI 가 쥐고 있으면 (호스트가 115200 으로 열었으면) 건너뛴다
bool cmdChIsEnabled(uint32_t ch)
{
#ifdef _USE_HW_CDC
  if (ch == CMD_CH_CDC)
  {
    return cdcIsConnect() == true && usbGetType() != USB_CON_CLI;
  }
#else
  (void)ch;
#endif
  return true;
}

void cmdTaskThread(void const *arg)
{
  (void)arg;
  cmdTaskUpdate();
}

#endif
