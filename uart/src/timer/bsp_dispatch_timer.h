#ifndef TIMER_BSP_DISPATCH_TIMER_H_
#define TIMER_BSP_DISPATCH_TIMER_H_

#include "hal_data.h"

/*
 * 模块说明：
 * 1. 本模块属于板级定时器驱动层，只封装 FSP 已配置好的 AGT0。
 * 2. 这层只负责把“10ms 周期到点”这件事往上转发，不在这里掺杂任何业务计数逻辑。
 * 3. 上层通过注册回调拿到 Tick 事件，不直接接触 g_timer_agt0。
 */
typedef void (*bsp_dispatch_timer_tick_callback_t)(void * p_context);

/*
 * 函数作用：
 *   初始化 AGT0，并登记一个上层 Tick 回调。
 * 调用时机：
 *   系统启动时由中间层调用一次。
 * 参数说明：
 *   p_callback: 10ms Tick 到来时要调用的上层函数，可为 NULL。
 *   p_context: 传递给回调函数的用户上下文指针，可为 NULL。
 * 返回值：
 *   FSP_SUCCESS 表示初始化成功，或者定时器本来就已经打开；
 *   其他错误码表示底层 open 失败。
 * 调用方式：
 *   先调用本函数，再调用 BSP_DispatchTimer_Start 启动定时器。
 */
fsp_err_t BSP_DispatchTimer_Init(bsp_dispatch_timer_tick_callback_t p_callback, void * p_context);

/*
 * 函数作用：
 *   启动已经初始化好的 AGT0 周期定时器。
 * 调用时机：
 *   初始化成功后立即调用；后续若曾停止，也可再次调用恢复运行。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示启动成功，或者定时器本来就已经处于运行状态；
 *   FSP_ERR_NOT_OPEN 表示尚未初始化；
 *   其他错误码表示底层 start 失败。
 * 调用方式：
 *   一般只由中间层统一调用。
 */
fsp_err_t BSP_DispatchTimer_Start(void);

/*
 * 函数作用：
 *   停止 AGT0 周期定时器。
 * 调用时机：
 *   当前工程通常不会主动停止，只有后续需要暂停调度节拍时才调用。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示停止成功，或者定时器本来就未运行；
 *   FSP_ERR_NOT_OPEN 表示尚未初始化；
 *   其他错误码表示底层 stop 失败。
 * 调用方式：
 *   调用后若需要恢复运行，应重新调用 BSP_DispatchTimer_Start。
 */
fsp_err_t BSP_DispatchTimer_Stop(void);

/*
 * 函数作用：
 *   FSP 为 AGT0 生成的中断回调入口。
 * 调用时机：
 *   不能手动调用，只会在 AGT0 周期到点时由底层自动触发。
 * 参数说明：
 *   p_args: FSP 传入的定时器回调参数。
 * 返回值：
 *   无。
 * 调用方式：
 *   保持函数名与 FSP 配置一致即可，由本模块内部继续把 Tick 事件转发给上层。
 */
void timer0_callback(timer_callback_args_t * p_args);

#endif /* TIMER_BSP_DISPATCH_TIMER_H_ */
