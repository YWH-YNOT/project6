#include "bsp_key_irq.h"

/*
 * 内部状态说明：
 * 1. g_bsp_key1_press_latched 用于锁存一次按下事件；
 * 2. g_bsp_key_irq_is_inited 用于防止重复初始化中断通道。
 */
static volatile bool g_bsp_key1_press_latched = false;
static bool          g_bsp_key_irq_is_inited  = false;

/*
 * 函数作用：
 *   打开并使能 SW2 对应的外部中断。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示初始化完成；
 *   其他错误码表示底层中断 open 或 enable 失败。
 * 调用方式：
 *   一般只在系统启动阶段调用一次。
 */
fsp_err_t BSP_KeyIrq_Init (void)
{
    fsp_err_t err = FSP_SUCCESS;

    if (g_bsp_key_irq_is_inited)
    {
        return FSP_SUCCESS;
    }

    err = R_ICU_ExternalIrqOpen(&g_external_irq6_ctrl, &g_external_irq6_cfg);
    if ((FSP_SUCCESS != err) && (FSP_ERR_ALREADY_OPEN != err))
    {
        return err;
    }

    err = R_ICU_ExternalIrqEnable(&g_external_irq6_ctrl);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_bsp_key_irq_is_inited = true;

    return FSP_SUCCESS;
}

/*
 * 函数作用：
 *   读取并清除一次按键按下事件。
 * 参数说明：
 *   无。
 * 返回值：
 *   true 表示本次确实消费到了一个按下事件；
 *   false 表示没有新的按键事件。
 * 调用方式：
 *   由中间层或应用层轮询调用，适合“按下事件只生效一次”的场景。
 */
bool BSP_KeyIrq_TryConsumeKey1Press (void)
{
    uint32_t primask = __get_PRIMASK();
    bool     pressed = false;

    /*
     * 这里临时关中断，是为了保证“读标志 + 清标志”是一个原子操作。
     * 否则主循环正准备清除事件时，外部中断可能又写入一次新事件，造成竞争。
     */
    __disable_irq();

    if (g_bsp_key1_press_latched)
    {
        g_bsp_key1_press_latched = false;
        pressed                  = true;
    }

    if (0U == primask)
    {
        __enable_irq();
    }

    return pressed;
}

/*
 * 函数作用：
 *   外部中断真实触发后的底层回调。
 * 参数说明：
 *   p_args: FSP 传入的外部中断参数。
 * 返回值：
 *   无。
 * 调用方式：
 *   由底层自动调用，不允许业务层手动调用。
 */
void key_external_irq_callback (external_irq_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    if (BSP_KEY_IRQ_CHANNEL == p_args->channel)
    {
        g_bsp_key1_press_latched = true;
    }
}
