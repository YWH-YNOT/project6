#include "bsp_key_irq.h"

/*
 * 按键事件只做“锁存”，由主循环读取并消费。
 * 这样可以保证中断足够轻，也能保持驱动层职责单一。
 */
static volatile bool g_bsp_key1_press_latched = false;
static bool          g_bsp_key_irq_is_inited  = false;

fsp_err_t BSP_KeyIrq_Init(void)
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

bool BSP_KeyIrq_TryConsumeKey1Press(void)
{
    uint32_t primask = __get_PRIMASK();
    bool     pressed = false;

    /*
     * 这里临时关中断，是为了避免主循环清标志的同时，
     * 外部中断又把同一个标志重新置位，导致事件丢失。
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

fsp_err_t Key_IRQ_Init(void)
{
    return BSP_KeyIrq_Init();
}

void key_external_irq_callback(external_irq_callback_args_t * p_args)
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
