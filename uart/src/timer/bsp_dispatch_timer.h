#ifndef TIMER_BSP_DISPATCH_TIMER_H_
#define TIMER_BSP_DISPATCH_TIMER_H_

#include "hal_data.h"

/*
 * 板级定时器驱动层：
 * 1. 这一层只负责把 FSP 生成的 AGT0 定时器实例封装起来；
 * 2. 应用层和中间层都不直接碰 g_timer_agt0，避免把 HAL 细节扩散出去；
 * 3. 定时器中断只往上层转发“10ms 到了”这件事，不在这里夹杂业务逻辑。
 */
typedef void (*bsp_dispatch_timer_tick_callback_t)(void * p_context);

/* 初始化 AGT0，并登记一个上层的 10ms Tick 回调。 */
fsp_err_t BSP_DispatchTimer_Init(bsp_dispatch_timer_tick_callback_t p_callback, void * p_context);
/* 启动已经初始化好的 AGT0 周期定时器。 */
fsp_err_t BSP_DispatchTimer_Start(void);
/* 停止已经启动的 AGT0 周期定时器。 */
fsp_err_t BSP_DispatchTimer_Stop(void);

/*
 * 这个函数名由 FSP 自动生成层约定。
 * AGT0 每 10ms 进一次中断时，会自动回调到这里。
 */
void timer0_callback(timer_callback_args_t * p_args);

#endif /* TIMER_BSP_DISPATCH_TIMER_H_ */
