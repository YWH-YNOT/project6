#ifndef TIMER_MID_DISPATCH_TIMER_H_
#define TIMER_MID_DISPATCH_TIMER_H_

#include "timer/bsp_dispatch_timer.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * 中间层定时器服务：
 * 1. 底层 AGT0 每 10ms 进一次中断；
 * 2. 中间层把 10ms Tick 聚合成 200ms 业务节拍；
 * 3. 应用层只关心“现在能不能发一次命令”，不关心 AGT0 寄存器和中断细节。
 */
#define MID_DISPATCH_TIMER_TICK_PERIOD_MS      10U
#define MID_DISPATCH_TIMER_DISPATCH_PERIOD_MS  200U

#if (MID_DISPATCH_TIMER_DISPATCH_PERIOD_MS % MID_DISPATCH_TIMER_TICK_PERIOD_MS) != 0
#error "MID_DISPATCH_TIMER_DISPATCH_PERIOD_MS 必须是 MID_DISPATCH_TIMER_TICK_PERIOD_MS 的整数倍"
#endif

/* 初始化并启动 10ms 周期定时器。 */
fsp_err_t MID_DispatchTimer_Init(void);
/* 轮询 200ms 发送标志，成功消费后返回 true。 */
bool MID_DispatchTimer_TryConsumeDispatchFlag(void);

#endif /* TIMER_MID_DISPATCH_TIMER_H_ */
