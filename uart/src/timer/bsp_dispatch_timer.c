#include "timer/bsp_dispatch_timer.h"

#include <stddef.h>
#include <stdbool.h>

/*
 * 板级层内部上下文：
 * 1. 只保存 AGT0 的打开/启动状态；
 * 2. 只保存一个上层 Tick 回调指针；
 * 3. 不保存任何“200ms 发一次”的业务计数，避免层间职责混乱。
 */
typedef struct st_bsp_dispatch_timer_context
{
    bool                              is_open;
    bool                              is_started;
    bsp_dispatch_timer_tick_callback_t p_tick_callback;
    void                            * p_tick_context;
} bsp_dispatch_timer_context_t;

static bsp_dispatch_timer_context_t g_bsp_dispatch_timer_context;

fsp_err_t BSP_DispatchTimer_Init (bsp_dispatch_timer_tick_callback_t p_callback, void * p_context)
{
    fsp_err_t err = FSP_SUCCESS;

    g_bsp_dispatch_timer_context.p_tick_callback = p_callback;
    g_bsp_dispatch_timer_context.p_tick_context  = p_context;

    /*
     * AGT0 只需要打开一次。
     * 后续如果上层重复初始化，只更新回调，不重复 open。
     */
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

void timer0_callback (timer_callback_args_t * p_args)
{
    /*
     * 这里只转发“一个 10ms 周期到点了”这件事。
     * 真正的 200ms 计数与发命令标志，由中间层继续封装。
     */
    if ((NULL == p_args) || (TIMER_EVENT_CYCLE_END != p_args->event))
    {
        return;
    }

    if (NULL != g_bsp_dispatch_timer_context.p_tick_callback)
    {
        g_bsp_dispatch_timer_context.p_tick_callback(g_bsp_dispatch_timer_context.p_tick_context);
    }
}
