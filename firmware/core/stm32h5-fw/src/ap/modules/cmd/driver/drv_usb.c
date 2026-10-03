#include "driver/drv_usb.h"

#if defined(_USE_HW_CMD) && defined(_USE_HW_CDC)


/* USB CDC cmd 채널 (w6300 drv_usb.c).

   CDC 스트림은 CLI 와 나눠 쓰지 않는다. 호스트가 연 보율로 주인이 갈린다 (cdcGetType()).
     115200 -> CLI (cli 모듈이 가져간다. cmd_task 는 이 채널을 건너뛴다)
     그 외   -> cmd (download.py 는 921600 으로 연다. USB 라 실제 속도와는 무관하다)
   둘이 같은 스트림을 각자 읽으면 서로 바이트를 훔쳐 양쪽 다 깨진다. */


static bool     drvUsbOpen(void *args);
static bool     drvUsbClose(void *args);
static uint32_t drvUsbAvailable(void *args);
static bool     drvUsbFlush(void *args);
static uint8_t  drvUsbRead(void *args);
static uint32_t drvUsbWrite(void *args, uint8_t *p_data, uint32_t length);




bool drvUsbInit(cmd_driver_t *p_driver)
{
  p_driver->open      = drvUsbOpen;
  p_driver->close     = drvUsbClose;
  p_driver->available = drvUsbAvailable;
  p_driver->flush     = drvUsbFlush;
  p_driver->read      = drvUsbRead;
  p_driver->write     = drvUsbWrite;
  p_driver->set_baud  = NULL;           // USB 는 보율이 없다
  p_driver->get_baud  = NULL;
  return true;
}

bool drvUsbOpen(void *args)
{
  (void)args;
  return true;
}

bool drvUsbClose(void *args)
{
  (void)args;
  return true;
}

uint32_t drvUsbAvailable(void *args)
{
  (void)args;
  return cdcAvailable();
}

bool drvUsbFlush(void *args)
{
  (void)args;
  while (cdcAvailable())
    cdcRead();
  return true;
}

uint8_t drvUsbRead(void *args)
{
  (void)args;
  return cdcRead();
}

uint32_t drvUsbWrite(void *args, uint8_t *p_data, uint32_t length)
{
  (void)args;
  return cdcWrite(p_data, length);
}

#endif
