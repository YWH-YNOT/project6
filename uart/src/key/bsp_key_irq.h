#ifndef KEY_BSP_KEY_IRQ_H_
#define KEY_BSP_KEY_IRQ_H_

//按键是irq6   p000
//led是      p400
#include "hal_data.h"

/*按键引脚定义
 * */

#define KEY1_SW2_PIN    BSP_IO_PORT_00_PIN_00


void Key_IRQ_Init(void);    //按键初始化函数


#endif /* KEY_BSP_KEY_IRQ_H_ */
