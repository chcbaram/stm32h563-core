#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#include "led.h"
#include "uart.h"
#include "cli.h"
#include "log.h"
#include "rtc.h"
#include "reset.h"
#include "fault.h"
#include "flash.h"
#include "qspi.h"
#include "cmd.h"
#include "usb.h"
#include "cdc.h"
#include "qbuffer.h"
#include "util_core.h"


bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif
