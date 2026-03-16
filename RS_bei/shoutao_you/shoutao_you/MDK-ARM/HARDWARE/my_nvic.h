#ifndef my_nvic_H_
#define my_nvic_H_

#include "headfile.h"

//extern变量声明
extern uint8_t uart6_data;
extern uint8_t uart2_data;
extern int t1_flag ;
extern volatile int move_flag;
extern volatile bool direction_lock_flag ; // 初始为true，表示可以检测方向

extern volatile bool trans_flag ; // 初始为true

void key_read(void);

#endif 
