#ifndef control_H
#define control_H

#include "headfile.h"
extern int mode1_flag;
extern int mode2_flag;
extern int mode3_flag;
extern int mode_start;
/*
                UP = 0x03         # 上升
    DOWN = 0x04       # 下降
    LEFT = 0x01       # 向左
    RIGHT = 0x02      # 向右
    LEFTUP = 0x0D     # 左上
    RIGHTUP = 0x17    # 右上
    LEFTDOWN = 0x0E   # 左下
    RIGHTDOWN = 0x18  # 右下
    FORWARD = 0x08    # 向前
    BACKWARD = 0x0A   # 向后
    SPEEDUP = 0x05    # 加速
    SPEEDDOWN = 0x09  # 减速
    ROBOTMODE = 0x21  # 机器人模式
    CARMODE = 0x22    # 小车模式
    ANTICLOCKWISE = 0x06  # 左旋转
    CLOCKWISE = 0x07      # 右旋转
    EXIT = 0xFF       # 退出指令（新增）
*/
// 精细手势处理：判断手的状态（张开/握拳）
void Gesture_Control_pro(void);

// 运动方向识别：8方向运动判断
void Gesture_Move(void);

// 统一9类手势识别（右 gesture_model.h 输出）
// label 0-4: 原有手势； label 5-8: 方向手势，自动发出方向命令
void Gesture_Run(void);
void Gesture_Test(void); // Gesture_Run() 的向后兼容别名

extern uint8_t current_cmd;
extern uint8_t next_cmd;
#endif
