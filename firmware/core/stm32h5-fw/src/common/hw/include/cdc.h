#ifndef CDC_H_
#define CDC_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"
#include "uart.h"


#ifdef _USE_HW_CDC



bool     cdcInit(void);
bool     cdcIsInit(void);
bool     cdcIsConnect(void);
uint32_t cdcAvailable(void);
uint8_t  cdcRead(void);
uint32_t cdcWrite(uint8_t *p_data, uint32_t length);
uint32_t cdcGetBaud(void);
uint8_t  cdcGetType(void);

extern uart_driver_t cdc_uart_driver;

#endif


#ifdef __cplusplus
}
#endif


#endif /* SRC_COMMON_HW_INCLUDE_CDC_H_ */
