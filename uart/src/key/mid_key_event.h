#ifndef KEY_MID_KEY_EVENT_H_
#define KEY_MID_KEY_EVENT_H_

#include "key/bsp_key_irq.h"

#include <stdbool.h>

/*
 * 按键中间层
 * 1. 向应用层暴露“按键事件”的通用语义；
 * 2. 后续如果按键来源从外部中断切成定时扫描，上层接口也不用改。
 */
fsp_err_t MID_KeyEvent_Init(void);
bool MID_KeyEvent_TryConsumeKey1Press(void);

#endif /* KEY_MID_KEY_EVENT_H_ */
