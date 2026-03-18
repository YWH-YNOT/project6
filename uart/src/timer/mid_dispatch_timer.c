#include "timer/mid_dispatch_timer.h"

#include <stddef.h>

/* 200ms 业务周期等于 20 个 10ms Tick。 */
#define MID_DISPATCH_TIMER_TICKS_PER_DISPATCH \
    (MID_DISPATCH_TIMER_DISPATCH_PERIOD_MS / MID_DISPATCH_TIMER_TICK_PERIOD_MS)

/*
 * 内部上下文说明：
 * 1. tick_count 负责累计已经到来的 10ms Tick 数；
 * 2. pending_count 表示当前还有多少个 200ms 周期尚未被应用层消费。
 */
typedef struct st_mid_dispatch_timer_context
{
    volatile uint8_t tick_count;
    volatile uint8_t pending_count;
} mid_dispatch_timer_context_t;

static mid_dispatch_timer_context_t g_mid_dispatch_timer_context;

/*
 * 内部函数作用：
 *   板级层每给一次 10ms Tick，本函数就更新一次 200ms 调度计数。
 * 调用关系：
 *   仅由 BSP_DispatchTimer_Init 注册给板级定时器。
 * 参数说明：
 *   p_context: 用户上下文，当前未使用，固定为 NULL。
 * 返回值：
 *   无。
 */
static void mid_dispatch_timer_on_tick (void * p_context)
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

/*
 * 函数作用：
 *   初始化中间层调度器并启动底层 10ms 定时器。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示初始化和启动都成功；
 *   其他错误码透传自板级定时器层。
 * 调用方式：
 *   系统启动阶段调用一次即可。
 */
fsp_err_t MID_DispatchTimer_Init (void)
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

/*
 * 函数作用：
 *   读取并消费一次 200ms 调度标志。
 * 参数说明：
 *   无。
 * 返回值：
 *   true 表示成功取到一个待消费的 200ms 周期；
 *   false 表示当前没有新的周期事件。
 * 调用方式：
 *   当前在 hal_entry 主循环中轮询调用，决定是否进入一次 SVM 分类周期。
 */
bool MID_DispatchTimer_TryConsumeDispatchFlag (void)
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

/*
 * 函数作用：
 *   清空累计 Tick 和待消费标志。
 * 参数说明：
 *   无。
 * 返回值：
 *   无。
 * 调用方式：
 *   当系统在识别模式和采集模式间切换时调用，避免旧节拍污染新模式。
 */
void MID_DispatchTimer_ClearDispatchFlags (void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();

    g_mid_dispatch_timer_context.tick_count    = 0U;
    g_mid_dispatch_timer_context.pending_count = 0U;

    if (0U == primask)
    {
        __enable_irq();
    }
}
