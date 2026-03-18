#ifndef TIMER_MID_DISPATCH_TIMER_H_
#define TIMER_MID_DISPATCH_TIMER_H_

#include "timer/bsp_dispatch_timer.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * 模块说明：
 * 1. 板级层 AGT0 每 10ms 产生一次 Tick；
 * 2. 本模块把 10ms Tick 累加成 200ms 的业务调度节拍；
 * 3. 应用层只需要关心“现在是否到了一个新的 200ms 分类周期”。
 */
#define MID_DISPATCH_TIMER_TICK_PERIOD_MS      10U
#define MID_DISPATCH_TIMER_DISPATCH_PERIOD_MS  200U

#if (MID_DISPATCH_TIMER_DISPATCH_PERIOD_MS % MID_DISPATCH_TIMER_TICK_PERIOD_MS) != 0
#error "MID_DISPATCH_TIMER_DISPATCH_PERIOD_MS 必须是 MID_DISPATCH_TIMER_TICK_PERIOD_MS 的整数倍"
#endif

/*
 * 函数作用：
 *   初始化并启动 10ms 周期定时器，同时准备好 200ms 调度计数。
 * 调用时机：
 *   系统启动时由应用层调用一次。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示定时器已经开始工作；
 *   其他错误码透传自板级定时器驱动层。
 * 调用方式：
 *   初始化成功后，应用层再通过 MID_DispatchTimer_TryConsumeDispatchFlag 轮询业务节拍。
 */
fsp_err_t MID_DispatchTimer_Init(void);

/*
 * 函数作用：
 *   读取并消费一次 200ms 调度标志。
 * 调用时机：
 *   主循环中定期轮询调用。
 * 参数说明：
 *   无。
 * 返回值：
 *   true 表示当前确实到达了一个尚未消费的 200ms 周期；
 *   false 表示当前没有新的周期事件。
 * 调用方式：
 *   每次成功返回 true，就意味着可以执行一次分类周期。
 */
bool MID_DispatchTimer_TryConsumeDispatchFlag(void);

/*
 * 函数作用：
 *   清空中间层内部累计的 Tick 和待消费调度标志。
 * 调用时机：
 *   运行模式切换时调用，防止旧模式的节拍状态串到新模式里。
 * 参数说明：
 *   无。
 * 返回值：
 *   无。
 * 调用方式：
 *   当前由 hal_entry 在识别模式和采集模式切换时调用。
 */
void MID_DispatchTimer_ClearDispatchFlags(void);

#endif /* TIMER_MID_DISPATCH_TIMER_H_ */
