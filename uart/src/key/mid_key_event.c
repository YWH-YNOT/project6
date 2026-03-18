#include "mid_key_event.h"

fsp_err_t MID_KeyEvent_Init(void)
{
    return BSP_KeyIrq_Init();
}

bool MID_KeyEvent_TryConsumeKey1Press(void)
{
    return BSP_KeyIrq_TryConsumeKey1Press();
}
