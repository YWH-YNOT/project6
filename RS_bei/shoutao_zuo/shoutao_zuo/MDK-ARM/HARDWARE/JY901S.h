#ifndef JY901S_H
#define JY901S_H

#include <stdint.h>
#include <stdbool.h>
#include "headfile.h"

// 定义传感器数据结构体，存储解析后的数据
typedef struct {
    // 加速度数据 (单位: m/s2)
    float acc_x;
    float acc_y;
    float acc_z;
    
    // 角速度数据 (单位: °/s)
    float gyro_x;
    float gyro_y;
    float gyro_z;
    
    // 角度数据 (单位: °)
    float angle_roll;   // 横滚角
    float angle_pitch;  // 俯仰角
    float angle_yaw;    // 偏航角
    
    // 温度数据 (单位: °C)
    float temperature;
    
    // 版本号
    uint16_t version;
    
    // 数据更新标志
    bool acc_updated;
    bool gyro_updated;
    bool angle_updated;
    bool temp_updated;
} JY901S_Data;

// 外部声明传感器数据结构体，供其他文件访问
extern JY901S_Data jy901s_data;

// 函数声明
void get_JY901S(uint8_t uart2_data);
void JY901S_Init(void);
HAL_StatusTypeDef JY901S_SetBaudrateTo115200(UART_HandleTypeDef *huart, uint32_t originalBaud);
uint8_t JY901S_CalibrateAcceleration(UART_HandleTypeDef *huart);
#endif // JY901S_H
    