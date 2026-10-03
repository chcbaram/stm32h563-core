#include "usb.h"

#ifdef _USE_HW_USB
#include "tusb.h"


//   VID 0x1209 는 pid.codes (오픈소스 하드웨어용 공용 VID). PID 는 레지스트리에서 비어 있는 것을 골랐다.
//   weact-h750 (B750 ~ B752) 과 같이 칩 번호를 대역으로 쓴다.
//
//   **PID 는 모드(부트로더 / 앱)가 아니라 USB 구성을 따른다.**
//
//     B563  CDC + HID          부트로더 · 앱 공통 (지금)
//     B564  CDC + HID + MSC    부트로더 UF2 (예약)
//     B565  (예약)              앱 구성이 달라질 때 (예: 다른 USB 기능이 붙을 때)
//     B566  CDC + HID + MSC    앱 MSC (예약)
//
//   부트로더와 앱의 구성이 같으므로 PID 도 같게 둔다. 그래야 웹(WebHID / Web Serial)에서 장치가 바뀌어도
//   한 번 받은 권한이 그대로 통하고, Windows 에서 COM 포트도 그대로다. 모드는 cmd INFO 의 mode 와 제품명으로 안다.
//   구성이 바뀌면(MSC 등) PID 를 바꾼다 — 같은 VID/PID 로 구성만 바뀌면 Windows 가 캐시한 디스크립터로 잘못 붙는다.
#define USB_VID           0x1209
#define USB_PID           0xB563
#define USB_BCD           0x0200


//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device =
{
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = USB_BCD,

  // CDC 가 IAD 를 쓰므로 디바이스 클래스는 MISC/COMMON/IAD 여야 한다.
  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0100,

  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,

  .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void)
{
  return (uint8_t const *)&desc_device;
}


//--------------------------------------------------------------------+
// HID Report Descriptor
//
//   vendor-defined usage page(0xFF00) 를 써야 OS 가 키보드/마우스로 오인하지 않고,
//   WebHID 의 장치 선택창에 잡힌다.
//--------------------------------------------------------------------+
uint8_t const desc_hid_report[] =
{
  TUD_HID_REPORT_DESC_GENERIC_INOUT(CFG_TUD_HID_EP_BUFSIZE)
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
  (void)instance;
  return desc_hid_report;
}


//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
enum
{
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_HID,
  ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define EPNUM_HID_OUT     0x03
#define EPNUM_HID_IN      0x83

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

uint8_t const desc_fs_configuration[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

  // CDC : itf, str, notif EP, notif size, out EP, in EP, size
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 16, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

  // HID : itf, str, protocol, report desc len, out EP, in EP, size, interval
  TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, 5, HID_ITF_PROTOCOL_NONE,
                           sizeof(desc_hid_report),
                           EPNUM_HID_OUT, EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 1),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
  (void)index;
  return desc_fs_configuration;
}


//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
enum
{
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_CDC,
  STRID_HID,
};

char const *string_desc_arr[] =
{
  (const char[]){0x09, 0x04},     // 0: English (0x0409)
  "BARAM",                        // 1: Manufacturer
  _DEF_BOARD_NAME,                // 2: Product
  NULL,                           // 3: Serial (UID 로 생성)
  _DEF_BOARD_NAME " CDC",         // 4
  _DEF_BOARD_NAME " HID",         // 5
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void)langid;
  size_t chr_count;

  switch (index)
  {
    case STRID_LANGID:
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;

    case STRID_SERIAL:
      chr_count = usbGetSerial(_desc_str + 1, 32);
      break;

    default:
      if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
        return NULL;
      {
        const char *str = string_desc_arr[index];
        size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;

        chr_count = strlen(str);
        if (chr_count > max_count)
          chr_count = max_count;

        for (size_t i = 0; i < chr_count; i++)
          _desc_str[1 + i] = str[i];
      }
      break;
  }

  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}

#endif
