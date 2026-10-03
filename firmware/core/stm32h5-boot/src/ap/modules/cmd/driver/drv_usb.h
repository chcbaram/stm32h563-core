#ifndef DRV_USB_H_
#define DRV_USB_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#if defined(_USE_HW_CMD) && defined(_USE_HW_CDC)

bool drvUsbInit(cmd_driver_t *p_driver);

#endif

#ifdef __cplusplus
}
#endif

#endif
