#include "module.h"


#ifdef _USE_HW_USB


/* USB (TinyUSB, CDC + HID).

   모듈로 연다. 부트로더는 bootUp() 이 앱으로 점프하면 moduleInit() 까지 오지 않으므로
   **부트로더에 머무를 때만** 열거된다. 호스트에 장치가 나타났다 곧바로 사라지는 일이 없다 (w6300 과 같은 이유).

   tud_task() 를 계속 불러야 열거와 전송이 진행된다. cli / cmd 보다 먼저 연다. */


static bool usbTaskInit(void);
static void usbTaskUpdate(void const *arg);


MODULE_DEF(usb)
{
  .name     = "usb",
  .priority = MODULE_PRI_HIGH,
  .init     = usbTaskInit,
  .update   = usbTaskUpdate,
};




bool usbTaskInit(void)
{
  bool ret;

  ret = usbInit();
#ifdef _USE_HW_CDC
  cdcInit();
#endif
  return ret;
}

void usbTaskUpdate(void const *arg)
{
  (void)arg;
  usbUpdate();
}

#endif
