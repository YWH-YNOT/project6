#include "timer/mid_dispatch_timer.h"

#include <stddef.h>

/*
 * 200ms 业务节拍 = 20 个 10ms Tick。
 * 单独定义成宏，后续如果要调整周期，只需要改一处。
 */
#define MID_DISPATCH_TIMER_TICKS_PER_DISPATCH \
    (MID_DISPATCH_TIMER_DISPATCH_PERIOD_MS / MID_DISPATCH_TIMER_TICK_PERIOD_MS)

typedef struct st_mid_dispatch_timer_context
{
    /*
     * tick_count 负责累计 10ms Tick；
     * pending_count 负责告诉应用层“还有多少个 200ms 周期尚未消费”。
     */
    volatile uint8_t tick_count;
    volatile uint8_t pending_count;
} mid_dispatch_timer_context_t;

static mid_dispatch_timer_context_t g_mid_dispatch_timer_context;

static void mid_dispatch_timer_on_tick(void * p_context)
{
    (void) p_context;

    g_mid_dispatch_timer_context.tick_count++;

    if (g_mid_dispatch_timer_context.tick_count < MID_DISPATCH_TIMER_TICKS_PER_DISPATCH)
    {
        return;
    }

    g_mid_dispatch_timer_context.tick_count = 0U;

    if (g_mid_dispatch_timer_context.pending_count < UINT8_MAX)
    {
        g_mid_dispatch_timer_context.pending_count++;
    }
}

fsp_err_t MID_DispatchTimer_Init(void)
{
    fsp_err_t err = FSP_SUCCESS;

    g_mid_dispatch_timer_context.tick_count    = 0U;
    g_mid_dispatch_timer_context.pending_count = 0U;

    err = BSP_DispatchTimer_Init(mid_dispatch_timer_on_tick, NULL);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return BSP_DispatchTimer_Start();
}

bool MID_DispatchTimer_TryConsumeDispatchFlag(void)
{
    uint32_t primask = __get_PRIMASK();
    bool     ready   = false;

    __disable_irq();

    if (g_mid_dispatch_timer_context.pending_count > 0U)
    {
        g_mid_dispatch_timer_context.pending_count--;
        ready = true;
    }

    if (0U == primask)
    {
        __enable_irq();
    }

    return ready;
}

void MID_DispatchTimer_ClearDispatchFlags(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();

    /*
     * 模式切换时把节拍计数和待消费标志一起清零，
     * 避免识别模式和采集模式互相继承对方留下的时序状态。
     */
    g_mid_dispatch_timer_context.tick_count    = 0U;
    g_mid_dispatch_timer_context.pending_count = 0U;

    if (0U == primask)
    {
        __enable_irq();
    }
}
