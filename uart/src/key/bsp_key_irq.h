#ifndef KEY_BSP_KEY_IRQ_H_
#define KEY_BSP_KEY_IRQ_H_

#include "hal_data.h"

#include <stdbool.h>

/*
 * 模块说明：
 * 1. 本模块属于板级按键中断驱动层，只负责把 SW2 配成 ICU 外部中断。
 * 2. 中断里只做“按下事件锁存”，不做模式切换等业务逻辑。
 * 3. 上层通过 TryConsume 接口拿到一次性事件，避免直接读取底层中断标志。
 */
#define KEY1_SW2_PIN         BSP_IO_PORT_00_PIN_00
#define BSP_KEY_IRQ_CHANNEL  6U

/*
 * 函数作用：
 *   初始化 SW2 对应的外部中断通道。
 * 调用时机：
 *   系统启动时由中间层调用一次。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示初始化成功，或者中断本来就已经可用；
 *   其他错误码表示 open/enable 失败。
 * 调用方式：
 *   初始化成功后，中断回调会自动锁存按键事件，上层再通过消费接口读取。
 */
fsp_err_t BSP_KeyIrq_Init(void);

/*
 * 函数作用：
 *   读取并消费一次“SW2 曾被按下”的锁存事件。
 * 调用时机：
 *   主循环轮询按键事件时调用。
 * 参数说明：
 *   无。
 * 返回值：
 *   true 表示自上次调用后至少发生过一次按下；
 *   false 表示当前没有新的按键事件。
 * 调用方式：
 *   每次调用都会把已锁存事件清掉，因此适合“按一次切一次状态”的业务。
 */
bool BSP_KeyIrq_TryConsumeKey1Press(void);

#endif /* KEY_BSP_KEY_IRQ_H_ */
