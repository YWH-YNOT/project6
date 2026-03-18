#include "mid_key_event.h"

/*
 * 函数作用：
 *   初始化按键事件中间层，本质上就是拉起底层按键中断驱动。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示初始化完成；
 *   其他错误码透传自板级驱动层。
 * 调用方式：
 *   应用层在启动阶段调用一次即可。
 */
fsp_err_t MID_KeyEvent_Init (void)
{
    return BSP_KeyIrq_Init();
}

/*
 * 函数作用：
 *   读取并消费一次按键事件。
 * 参数说明：
 *   无。
 * 返回值：
 *   true 表示当前确实取到了一次按下事件；
 *   false 表示没有新事件。
 * 调用方式：
 *   应用层只需要关心“是否按过一次”，而不需要关心中断标志本身。
 */
bool MID_KeyEvent_TryConsumeKey1Press (void)
{
    return BSP_KeyIrq_TryConsumeKey1Press();
}
