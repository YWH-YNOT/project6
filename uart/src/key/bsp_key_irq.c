#include "bsp_key_irq.h"

/*KEY外部中断初始化函数*/
void Key_IRQ_Init(void)
{
    //系统错误类型变量，用来自动检测错误
    fsp_err_t err = FSP_SUCCESS;
    //打开ICU模块
    err = R_ICU_ExternalIrqOpen(&g_external_irq6_ctrl, &g_external_irq6_cfg);
    //使能中断
    err = R_ICU_ExternalIrqEnable(&g_external_irq6_ctrl);

    assert(FSP_SUCCESS == err);
}


volatile bool key1_sw2_press = false;

void key_external_irq_callback(external_irq_callback_args_t *p_args)
{
    if(p_args->channel == 6)
    {
        key1_sw2_press = true;  //按键1被按下

    }

}

