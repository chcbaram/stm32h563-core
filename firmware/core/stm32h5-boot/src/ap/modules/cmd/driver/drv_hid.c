#include "driver/drv_hid.h"

#if defined(_USE_HW_CMD) && defined(_USE_HW_USB)
#include "usb.h"
#include "qbuffer.h"

#if CFG_TUD_HID


/* USB HID cmd 채널 (w6300 drv_hid.c).

   HID 는 스트림이 아니라 64 바이트 고정 리포트다. cmd.c 는 바이트 스트림을 기대하므로
   수신 리포트를 링버퍼에 풀어 넣고, 송신은 63 바이트씩 잘라 보낸다.

     리포트 [0]  = 유효 바이트 수 (1~63)
     리포트 [1:] = 페이로드

   길이를 앞에 두는 이유 : HID 는 항상 64 바이트를 꽉 채워 보내므로 패딩과 데이터를 구분해야 한다.
   패딩을 그대로 흘리면 cmd.c 의 패킷 파서가 쓰레기 바이트를 먹는다.

   CDC 와 달리 전용 채널이라 CLI 와 다툴 일이 없다. 터미널이 CDC 를 CLI 로 쓰는 동안에도 동작한다. */
#define HID_RPT_SIZE      CFG_TUD_HID_EP_BUFSIZE      // 64
#define HID_PAYLOAD_MAX   (HID_RPT_SIZE - 1)          // 63
#define HID_RX_BUF_SIZE   2048


static bool     drvHidOpen(void *args);
static bool     drvHidClose(void *args);
static uint32_t drvHidAvailable(void *args);
static bool     drvHidFlush(void *args);
static uint8_t  drvHidRead(void *args);
static uint32_t drvHidWrite(void *args, uint8_t *p_data, uint32_t length);

static uint8_t   rx_buf[HID_RX_BUF_SIZE];
static qbuffer_t rx_q;
static bool      is_init = false;




bool drvHidInit(cmd_driver_t *p_driver)
{
  qbufferCreate(&rx_q, rx_buf, HID_RX_BUF_SIZE);
  is_init = true;

  p_driver->open      = drvHidOpen;
  p_driver->close     = drvHidClose;
  p_driver->available = drvHidAvailable;
  p_driver->flush     = drvHidFlush;
  p_driver->read      = drvHidRead;
  p_driver->write     = drvHidWrite;
  p_driver->set_baud  = NULL;
  p_driver->get_baud  = NULL;
  return true;
}

// TinyUSB 가 OUT 리포트를 받으면 부른다 (usb_hid.c). USB 콜백이라 링버퍼에 넣기만 한다.
void drvHidRxReport(uint8_t const *buffer, uint16_t bufsize)
{
  uint8_t n;

  if (is_init != true || bufsize < 1)
    return;

  n = buffer[0];
  if (n > HID_PAYLOAD_MAX)
    n = HID_PAYLOAD_MAX;
  if (n > bufsize - 1)
    n = (uint8_t)(bufsize - 1);

  qbufferWrite(&rx_q, (uint8_t *)&buffer[1], n);
}

bool drvHidOpen(void *args)
{
  (void)args;
  return true;
}

bool drvHidClose(void *args)
{
  (void)args;
  return true;
}

uint32_t drvHidAvailable(void *args)
{
  (void)args;
  return qbufferAvailable(&rx_q);
}

bool drvHidFlush(void *args)
{
  (void)args;
  qbufferFlush(&rx_q);
  return true;
}

uint8_t drvHidRead(void *args)
{
  uint8_t data = 0;

  (void)args;
  qbufferRead(&rx_q, &data, 1);
  return data;
}

uint32_t drvHidWrite(void *args, uint8_t *p_data, uint32_t length)
{
  uint32_t sent = 0;

  (void)args;

  if (usbIsInit() != true)
    return 0;

  while (sent < length)
  {
    uint8_t  rpt[HID_RPT_SIZE];
    uint32_t n = length - sent;
    uint32_t pre_time;

    if (n > HID_PAYLOAD_MAX)
      n = HID_PAYLOAD_MAX;

    memset(rpt, 0, sizeof(rpt));
    rpt[0] = (uint8_t)n;
    memcpy(&rpt[1], &p_data[sent], n);

    // IN 엔드포인트가 빌 때까지 기다린다. tud_task() 를 계속 돌려야 비워진다
    pre_time = millis();
    while (tud_hid_ready() != true)
    {
      usbUpdate();
      if (millis() - pre_time >= 100)
        return sent;
    }

    if (tud_hid_report(0, rpt, sizeof(rpt)) != true)
      return sent;

    sent += n;
  }
  return sent;
}

#endif
#endif
