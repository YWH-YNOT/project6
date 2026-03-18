#ifndef KEY_MID_KEY_EVENT_H_
#define KEY_MID_KEY_EVENT_H_

#include "key/bsp_key_irq.h"

#include <stdbool.h>

/*
 * 模块说明：
 * 1. 本模块属于按键事件中间层，把底层“中断已触发”重新包装成业务可读的“按键事件”。
 * 2. 后续如果按键来源从外部中断切成轮询扫描，上层接口依然可以保持不变。
 */

/*
 * 函数作用：
 *   初始化按键事件中间层。
 * 调用时机：
 *   系统启动时由应用层调用一次。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示初始化成功；
 *   其他错误码透传自 BSP_KeyIrq_Init。
 * 调用方式：
 *   成功后即可通过 MID_KeyEvent_TryConsumeKey1Press 轮询事件。
 */
fsp_err_t MID_KeyEvent_Init(void);

/*
 * 函数作用：
 *   读取并消费一次“按键被按下”的抽象事件。
 * 调用时机：
 *   主循环中定期轮询调用。
 * 参数说明：
 *   无。
 * 返回值：
 *   true 表示自上次调用后发生过一次按键按下；
 *   false 表示当前没有新的按键事件。
 * 调用方式：
 *   当前用于运行模式切换逻辑，不需要关心底层中断细节。
 */
bool MID_KeyEvent_TryConsumeKey1Press(void);

#endif /* KEY_MID_KEY_EVENT_H_ */
