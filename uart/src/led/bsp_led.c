#include "bsp_led.h"

/*
 * 函数作用：
 *   拉起 LED 相关引脚配置，让后续 LED 宏可以正常工作。
 * 参数说明：
 *   无。
 * 返回值：
 *   无。
 * 调用方式：
 *   本函数通常在 hal_entry 启动阶段调用一次，之后就可以直接操作 LED1 宏。
 */
void LED_Init (void)
{
    R_IOPORT_Open(&g_ioport_ctrl, g_ioport.p_cfg);
}
