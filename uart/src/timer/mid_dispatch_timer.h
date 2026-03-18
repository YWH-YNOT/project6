#ifndef TIMER_MID_DISPATCH_TIMER_H_
#define TIMER_MID_DISPATCH_TIMER_H_

#include "timer/bsp_dispatch_timer.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * 定时调度中间层
 * 1. 底层 AGT0 每 10ms 产生一次中断；
 * 2. 中间层把 10ms Tick 聚合成 200ms 的业务调度节拍；
 * 3. 应用层只关心“是否到了一次分类周期”，不需要关心具体寄存器细节。
 */
#define MID_DISPATCH_TIMER_TICK_PERIOD_MS      10U
#define MID_DISPATCH_TIMER_DISPATCH_PERIOD_MS  200U

#if (MID_DISPATCH_TIMER_DISPATCH_PERIOD_MS % MID_DISPATCH_TIMER_TICK_PERIOD_MS) != 0
#error "MID_DISPATCH_TIMER_DISPATCH_PERIOD_MS 必须是 MID_DISPATCH_TIMER_TICK_PERIOD_MS 的整数倍"
#endif

/* 初始化并启动 10ms 周期定时器。 */
fsp_err_t MID_DispatchTimer_Init(void);
/* 轮询 200ms 调度标志，成功消费后返回 true。 */
bool MID_DispatchTimer_TryConsumeDispatchFlag(void);
/* 模式切换时清空累计的调度标志，避免旧节拍影响新模式。 */
void MID_DispatchTimer_ClearDispatchFlags(void);

#endif /* TIMER_MID_DISPATCH_TIMER_H_ */
