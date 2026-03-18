#ifndef KEY_BSP_KEY_IRQ_H_
#define KEY_BSP_KEY_IRQ_H_

#include "hal_data.h"

#include <stdbool.h>

/*
 * 板级按键中断驱动层
 * 1. 只负责把 SW2 配成 ICU 外部中断；
 * 2. 中断里只锁存“按下事件”，不掺杂业务逻辑；
 * 3. 上层通过 TryConsume 接口读取一次性事件，避免直接碰中断标志。
 */
#define KEY1_SW2_PIN         BSP_IO_PORT_00_PIN_00
#define BSP_KEY_IRQ_CHANNEL  6U

/* 初始化 SW2 对应的外部中断。重复调用时直接返回成功。 */
fsp_err_t BSP_KeyIrq_Init(void);
/* 读取并消费一次按下事件。返回 true 表示自上次读取后至少按下过一次。 */
bool BSP_KeyIrq_TryConsumeKey1Press(void);

/*
 * 兼容旧接口名。
 * 新代码请优先使用 BSP_KeyIrq_Init()。
 */
fsp_err_t Key_IRQ_Init(void);

#endif /* KEY_BSP_KEY_IRQ_H_ */
