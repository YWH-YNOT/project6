#ifndef LED_BSP_LED_H_
#define LED_BSP_LED_H_

#include "hal_data.h"

/*
 * 宏作用：
 *   直接控制开发板 LED1 的电平状态。
 * 使用说明：
 *   1. 当前硬件为低电平点亮，因此 ON/OFF 的电平与直觉相反；
 *   2. 使用这些宏前，应先调用 LED_Init 完成 IOPORT 初始化；
 *   3. 这些宏不做返回值检查，适合当前简单状态指示场景。
 */
#define LED1_ON     R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_04_PIN_00, BSP_IO_LEVEL_LOW);
#define LED1_OFF    R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_04_PIN_00, BSP_IO_LEVEL_HIGH);
#define LED1_TOGGLE R_PORT4->PODR ^= 1 << (BSP_IO_PORT_04_PIN_00 & 0xFF)

/*
 * 函数作用：
 *   初始化 LED 所需的 IOPORT 配置。
 * 调用时机：
 *   系统启动后、第一次操作 LED 前调用一次即可。
 * 参数说明：
 *   无。
 * 返回值：
 *   无。
 * 调用方式：
 *   调用本函数后，即可使用 LED1_ON、LED1_OFF、LED1_TOGGLE 三个宏控制 LED。
 */
void LED_Init(void);

#endif /* LED_BSP_LED_H_ */
