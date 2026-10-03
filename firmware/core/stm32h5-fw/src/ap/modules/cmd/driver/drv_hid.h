#ifndef DRV_HID_H_
#define DRV_HID_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#if defined(_USE_HW_CMD) && defined(_USE_HW_USB)

bool drvHidInit(cmd_driver_t *p_driver);
void drvHidRxReport(uint8_t const *buffer, uint16_t bufsize);

#endif

#ifdef __cplusplus
}
#endif

#endif
