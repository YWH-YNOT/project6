#ifndef LED_BSP_LED_H_
#define LED_BSP_LED_H_

#include "hal_data.h"

#define LED1_ON     R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_04_PIN_00, BSP_IO_LEVEL_LOW);
#define LED1_OFF    R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_04_PIN_00, BSP_IO_LEVEL_HIGH);
#define LED1_TOGGLE R_PORT4->PODR ^=1<<(BSP_IO_PORT_04_PIN_00 & 0xFF)

void LED_Init(void);

#endif /* LED_BSP_LED_H_ */
