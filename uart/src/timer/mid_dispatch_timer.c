#include "timer/mid_dispatch_timer.h"

#include <stddef.h>

/* 100ms 分类周期等于 10 个 10ms Tick。 */
#define MID_DISPATCH_TIMER_TICKS_PER_CLASSIFY \
    (MID_DISPATCH_TIMER_CLASSIFY_PERIOD_MS / MID_DISPATCH_TIMER_TICK_PERIOD_MS)

/* 200ms 发送周期等于 20 个 10ms Tick。 */
#define MID_DISPATCH_TIMER_TICKS_PER_SEND \
    (MID_DISPATCH_TIMER_SEND_PERIOD_MS / MID_DISPATCH_TIMER_TICK_PERIOD_MS)

/*
 * 内部上下文说明：
 * 1. classify_tick_count 负责累计距离上一个 100ms 分类窗口已经过去了多少个 10ms Tick；
 * 2. send_tick_count 负责累计距离上一个 200ms 发送窗口已经过去了多少个 10ms Tick；
 * 3. classify_ready / send_ready 分别表示当前是否存在一个尚未被应用层消费的分类窗口 / 发送窗口。
 *
 * 设计说明：
 * 1. 这里故意不累计“待消费周期个数”，而只保留两个锁存标志；
 * 2. 这样即使主循环一段时间没有及时消费，也不会在恢复后连续补跑多个旧周期；
 * 3. 最终效果是：
 *    - 分类频率被限制为“最多每 100ms 一次”
 *    - 发送频率被限制为“最多每 200ms 一次”
 */
typedef struct st_mid_dispatch_timer_context
{
    volatile uint8_t classify_tick_count;
    volatile uint8_t send_tick_count;
    volatile bool    classify_ready;
    volatile bool    send_ready;
} mid_dispatch_timer_context_t;

static mid_dispatch_timer_context_t g_mid_dispatch_timer_context;

/*
 * 内部函数作用：
 *   板级层每给一次 10ms Tick，本函数就同步更新 100ms 分类节拍和 200ms 发送节拍。
 * 调用关系：
 *   仅由 BSP_DispatchTimer_Init 注册给板级定时器。
 * 参数说明：
 *   p_context: 用户上下文，当前未使用，固定为 NULL。
 * 返回值：
 *   无。
 * 调度策略说明：
 *   1. 每累计满 10 个 10ms Tick，就锁存一次 classify_ready；
 *   2. 每累计满 20 个 10ms Tick，就锁存一次 send_ready；
 *   3. 如果应用层还没来得及消费，这里也不会累计多个待处理周期，而只保留“有一个窗口待处理”这件事。
 */
static void mid_dispatch_timer_on_tick (void * p_context)
{
    (void) p_context;

    g_mid_dispatch_timer_context.classify_tick_count++;
    g_mid_dispatch_timer_context.send_tick_count++;

    if (g_mid_dispatch_timer_context.classify_tick_count >= MID_DISPATCH_TIMER_TICKS_PER_CLASSIFY)
    {
        g_mid_dispatch_timer_context.classify_tick_count = 0U;
        g_mid_dispatch_timer_context.classify_ready      = true;
    }

    if (g_mid_dispatch_timer_context.send_tick_count >= MID_DISPATCH_TIMER_TICKS_PER_SEND)
    {
        g_mid_dispatch_timer_context.send_tick_count = 0U;
        g_mid_dispatch_timer_context.send_ready      = true;
    }
}

/*
 * 内部函数作用：
 *   在关中断保护下消费一个布尔节拍标志。
 * 调用关系：
 *   仅供本文件内部两个对外 TryConsume 接口复用。
 * 参数说明：
 *   p_flag: 待消费的节拍标志地址。
 * 返回值：
 *   true 表示成功消费到了一个待处理窗口；
 *   false 表示当前没有待处理窗口。
 */
static bool mid_dispatch_timer_try_consume_flag (volatile bool * p_flag)
{
    uint32_t primask = __get_PRIMASK();
    bool     ready   = false;

    if (NULL == p_flag)
    {
        return false;
    }

    __disable_irq();

    if (*p_flag)
    {
        *p_flag = false;
        ready   = true;
    }

    if (0U == primask)
    {
        __enable_irq();
    }

    return ready;
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

    g_mid_dispatch_timer_context.classify_tick_count = 0U;
    g_mid_dispatch_timer_context.send_tick_count     = 0U;
    g_mid_dispatch_timer_context.classify_ready      = false;
    g_mid_dispatch_timer_context.send_ready          = false;

    err = BSP_DispatchTimer_Init(mid_dispatch_timer_on_tick, NULL);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return BSP_DispatchTimer_Start();
}

/*
 * 函数作用：
 *   读取并消费一次 100ms 分类节拍标志。
 * 参数说明：
 *   无。
 * 返回值：
 *   true 表示成功取到一个待消费的 100ms 分类周期；
 *   false 表示当前没有新的周期事件。
 * 调用方式：
 *   当前在 hal_entry 主循环中轮询调用，决定是否进入一次新的 SVM 分类周期。
 */
bool MID_DispatchTimer_TryConsumeClassifyFlag (void)
{
    return mid_dispatch_timer_try_consume_flag(&g_mid_dispatch_timer_context.classify_ready);
}

/*
 * 函数作用：
 *   读取并消费一次 200ms 发送节拍标志。
 * 参数说明：
 *   无。
 * 返回值：
 *   true 表示成功取到一个待消费的 200ms 发送周期；
 *   false 表示当前没有新的发送窗口。
 * 调用方式：
 *   当前在 hal_entry 主循环中轮询调用，决定是否真正发出一次命令字节。
 */
bool MID_DispatchTimer_TryConsumeSendFlag (void)
{
    return mid_dispatch_timer_try_consume_flag(&g_mid_dispatch_timer_context.send_ready);
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

    g_mid_dispatch_timer_context.classify_tick_count = 0U;
    g_mid_dispatch_timer_context.send_tick_count     = 0U;
    g_mid_dispatch_timer_context.classify_ready      = false;
    g_mid_dispatch_timer_context.send_ready          = false;

    if (0U == primask)
    {
        __enable_irq();
    }
}
