#ifndef __MPU6050_H
#define __MPU6050_H

#include "headfile.h"  // 需包含 STM32 HAL 等基础头文件

// MPU6050 设备地址（AD0 接 GND 时为 0xD2）
#define MPU6050_ADDR 0xD2
// 寄存器地址
#define PWR_MGMT_1      0x6B
#define SMPLRT_DIV      0x19
#define CONFIG          0x1A
#define GYRO_CONFIG     0x1B
#define ACCEL_CONFIG    0x1C
#define INT_ENABLE      0x38
#define ACCEL_XOUT_H    0x3B
#define GYRO_XOUT_H     0x43
// 物理参数
#define M_PI            3.1415926
#define ACCEL_SCALE     16384.0f   // 加速度计 ±2g 量程：16384 LSB/g
#define GYRO_SCALE      131.0f     // 陀螺仪 ±250°/s 量程：131 LSB/(°/s)
#define ALPHA           0.05f      // 互补滤波系数（X、Y 轴）
#define ALPHA_Z         0.93f      // 互补滤波系数（Z 轴）


#define LOCK_DURATION 1000                // 锁定持续时间(ms)
#define UNLOCK_DELAY_FRAMES 3             // 解锁后延迟帧数（新增）
#define CMD_LEFT    0x06  // 左
#define CMD_RIGHT   0x07  // 右
#define CMD_UP      0x08  // 上
#define CMD_DOWN    0x0A  // 下

#define CMD_LEFTUP    	0x0D  // 左上
#define CMD_RIGHTUP   	0x17  // 右上
#define CMD_LEFTDOWN   	0x0E  // 左下
#define CMD_RIGHTDOWN   0x18  // 右下

#define CMD_FORWARD   	0x08  // 向前			手势3，上面的按键
#define CMD_BACKWARD    0x0A  // 向后			手势3，下面的按键

#define CMD_ROBOTMODE  0x21  // 机器人
#define CMD_CARMODE    0x22  // 小车
#define CMD_NONE    0x00  // 无指令

#define CMD_ANJIAN_SHIJUE 	0x67			//手指比1，下面的按键   //检测1 手势1
#define CMD_KAIGUAN_SHIJUE  0x68			//手指比2，下						//检测2 手势2
#define CMD_ANJIAN_DONGZUO   0x05			//手指比1，上面的按键	  //动作1 手势3
#define CMD_KAIGUAN_DONGZUO  0x19			//手指比2，上


#define EXIT 0xFF					//退出
	
// 存储单个 MPU6050 的所有数据（原始值、物理单位、角度）
typedef struct 
{
    // 原始数据（16位整数）
    int16_t ax_raw;    // 加速度计 X 轴原始值
    int16_t ay_raw;    // 加速度计 Y 轴原始值
    int16_t az_raw;    // 加速度计 Z 轴原始值
    int16_t gx_raw;    // 陀螺仪 X 轴原始值
    int16_t gy_raw;    // 陀螺仪 Y 轴原始值
    int16_t gz_raw;    // 陀螺仪 Z 轴原始值

    // 物理单位数据
    float accel_x;     // 加速度计 X 轴（单位：g）
    float accel_y;     // 加速度计 Y 轴（单位：g）
    float accel_z;     // 加速度计 Z 轴（单位：g）
    float gyro_x;      // 陀螺仪 X 轴（单位：°/s）
    float gyro_y;      // 陀螺仪 Y 轴（单位：°/s）
    float gyro_z;      // 陀螺仪 Z 轴（单位：°/s）

    // 角度数据
    float accel_angle_x;  // 加速度计计算的 X 轴角度（°）
    float accel_angle_y;  // 加速度计计算的 Y 轴角度（°）
    float filter_angle_x; // 互补滤波后的 X 轴角度（°）
    float filter_angle_y; // 互补滤波后的 Y 轴角度（°）
    float filter_angle_z; // 互补滤波后的 Z 轴角度（°）

    uint32_t last_sample_time; // 上一次采样时间（用于计算 dt）
		
		// 新增：运动加速度（m/s²）和速度（m/s）
    float motion_accel_x;  // 分离重力后的X轴运动加速度
    float motion_accel_y;  // 分离重力后的Y轴运动加速度
    float motion_accel_z;  // 分离重力后的Z轴运动加速度
    float velocity_x;      // X轴速度
    float velocity_y;      // Y轴速度
    float velocity_z;      // Z轴速度

    // 新增：加速度零漂校准参数
    float accel_offset_x;  // X轴加速度零漂（m/s²）
    float accel_offset_y;  // Y轴加速度零漂
    float accel_offset_z;  // Z轴加速度零漂
    uint8_t accel_calibrated;  // 校准完成标志（0=未校准，1=已校准）

    // 新增：辅助变量（静止判断）
    float last_filter_angle_x;  // 上一时刻X轴滤波角度
    float last_filter_angle_y;  // 上一时刻Y轴滤波角度
} MPU6050_Data_t;

// 全局变量：存储 0-5 号通道的 MPU6050 数据（共 6 个通道）
extern MPU6050_Data_t mpu_data[5];  

extern uint8_t current_cmd ;
extern uint8_t next_cmd ;
// 函数声明
// 函数声明
void MPU6050_Init(I2C_HandleTypeDef *hi2c);
void MPU6050_ReadRawData(I2C_HandleTypeDef *hi2c, uint8_t channel);
void MPU6050_CalcPhysicalData(uint8_t channel);
void MPU6050_CalcAccelAngle(uint8_t channel);
void MPU6050_ComplementaryFilter(uint8_t channel);
void MPU6050_ReadSingleChannel(uint8_t channel);
void MPU6050_PollChannels(const uint8_t channels[], uint8_t channel_num, uint16_t delay_ms , uint8_t channel_cnt);
// 控制相关函数已移至control.c以提高可移植性
// - Gesture_Control(): 已废弃，请使用control.c中的Gesture_Control_pro()
// - Gesture_Move(): 已移至control.c，请包含control.h头文件使用

// 加速度计零漂校准（单通道）
void MPU6050_CalibrateAccel(uint8_t channel, uint16_t samples);
// 计算运动加速度和速度（单通道）
void MPU6050_CalcMotionAndVelocity(uint8_t channel);

#endif