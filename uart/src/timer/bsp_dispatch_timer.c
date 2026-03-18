#include "timer/bsp_dispatch_timer.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * 内部上下文说明：
 * 1. is_open 表示 AGT0 是否已经 open；
 * 2. is_started 表示 AGT0 是否已经 start；
 * 3. p_tick_callback/p_tick_context 保存上层注册的 10ms Tick 通知接口。
 */
typedef struct st_bsp_dispatch_timer_context
{
    bool                               is_open;
    bool                               is_started;
    bsp_dispatch_timer_tick_callback_t p_tick_callback;
    void                             * p_tick_context;
} bsp_dispatch_timer_context_t;

static bsp_dispatch_timer_context_t g_bsp_dispatch_timer_context;

/*
 * 函数作用：
 *   初始化 AGT0，并登记上层回调。
 * 参数说明：
 *   p_callback: 上层回调函数。
 *   p_context: 回调上下文。
 * 返回值：
 *   FSP_SUCCESS 表示初始化成功；
 *   其他错误码表示底层 open 失败。
 * 调用方式：
 *   当前由中间层 MID_DispatchTimer_Init 统一调用。
 */
fsp_err_t BSP_DispatchTimer_Init (bsp_dispatch_timer_tick_callback_t p_callback, void * p_context)
{
    fsp_err_t err = FSP_SUCCESS;

    g_bsp_dispatch_timer_context.p_tick_callback = p_callback;
    g_bsp_dispatch_timer_context.p_tick_context  = p_context;

    if (g_bsp_dispatch_timer_context.is_open)
    {
        return FSP_SUCCESS;
    }

    err = g_timer_agt0.p_api->open(g_timer_agt0.p_ctrl, g_timer_agt0.p_cfg);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_bsp_dispatch_timer_context.is_open = true;

    return FSP_SUCCESS;
}

/*
 * 函数作用：
 *   启动 AGT0 周期定时器。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示启动成功；
 *   FSP_ERR_NOT_OPEN 表示尚未初始化；
 *   其他错误码表示底层启动失败。
 * 调用方式：
 *   在 BSP_DispatchTimer_Init 成功后调用。
 */
fsp_err_t BSP_DispatchTimer_Start (void)
{
    fsp_err_t err = FSP_SUCCESS;

    if (!g_bsp_dispatch_timer_context.is_open)
    {
        return FSP_ERR_NOT_OPEN;
    }

    if (g_bsp_dispatch_timer_context.is_started)
    {
        return FSP_SUCCESS;
    }

    err = g_timer_agt0.p_api->start(g_timer_agt0.p_ctrl);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_bsp_dispatch_timer_context.is_started = true;

    return FSP_SUCCESS;
}

/*
 * 函数作用：
 *   停止 AGT0 周期定时器。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示停止成功；
 *   FSP_ERR_NOT_OPEN 表示尚未初始化；
 *   其他错误码表示底层停止失败。
 * 调用方式：
 *   当前工程默认不主动调用，只保留扩展能力。
 */
fsp_err_t BSP_DispatchTimer_Stop (void)
{
    fsp_err_t err = FSP_SUCCESS;

    if (!g_bsp_dispatch_timer_context.is_open)
    {
        return FSP_ERR_NOT_OPEN;
    }

    if (!g_bsp_dispatch_timer_context.is_started)
    {
        return FSP_SUCCESS;
    }

    err = g_timer_agt0.p_api->stop(g_timer_agt0.p_ctrl);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_bsp_dispatch_timer_context.is_started = false;

    return FSP_SUCCESS;
}

/*
 * 函数作用：
 *   处理 AGT0 的周期中断，并把“10ms 到点”事件转发给上层。
 * 参数说明：
 *   p_args: FSP 定时器回调参数。
 * 返回值：
 *   无。
 * 调用方式：
 *   由底层自动调用，上层不直接触碰本函数。
 */
void timer0_callback (timer_callback_args_t * p_args)
{
    if ((NULL == p_args) || (TIMER_EVENT_CYCLE_END != p_args->event))
    {
        return;
    }

    if (NULL != g_bsp_dispatch_timer_context.p_tick_callback)
    {
        g_bsp_dispatch_timer_context.p_tick_callback(g_bsp_dispatch_timer_context.p_tick_context);
    }
}
