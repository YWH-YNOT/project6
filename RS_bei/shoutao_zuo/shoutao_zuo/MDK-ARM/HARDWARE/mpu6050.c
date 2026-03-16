#include "headfile.h"
#include "mpu6050.h"

int x_dir = 0;
int y_dir = 0;
		
float last_acc_delta_x,last_acc_delta_y,last_acc_delta_z = 0;
float last_acc_num = 0;

float last_acc_delta_x1,last_acc_delta_y1,last_acc_delta_z1 = 0;
uint8_t current_cmd = CMD_NONE;
uint8_t next_cmd = CMD_NONE;
// 时间锁相关变量（新增：解锁后3帧延迟计数器）
static uint32_t direction_lock_time = 0;  // 方向锁定开始时间
static uint8_t direction_locked = 0;      // 0:未锁定 1:已锁定
static uint8_t unlock_delay_cnt = 0;      // 解锁后数据计数器（需满3帧）

int step_flag = 0;														//状态机标志位
// 全局变量定义：初始化 6 个通道的 MPU 数据
MPU6050_Data_t mpu_data[5] = {0};  

/**
 * @brief  初始化单个 MPU6050（需先切换到对应通道）
 * @param  hi2c：I2C 句柄
 */
void MPU6050_Init(I2C_HandleTypeDef *hi2c)
{
    uint8_t data;

    // 唤醒 MPU6050（PWR_MGMT_1 寄存器：0x00 禁用睡眠模式）
    data = 0x00;
    HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, PWR_MGMT_1, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
    HAL_Delay(100);

    // 设置采样率（SMPLRT_DIV = 0x07 → 采样率 = 1kHz/(1+7) = 125Hz）
    data = 0x07;
    HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, SMPLRT_DIV, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);

    // 配置低通滤波（CONFIG = 0x00 → 256Hz 带宽）
    data = 0x00;
    HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, CONFIG, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);

    // 配置陀螺仪量程（GYRO_CONFIG = 0x00 → ±250°/s）
    data = 0x00;
    HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, GYRO_CONFIG, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);

    // 配置加速度计量程（ACCEL_CONFIG = 0x00 → ±2g）
    data = 0x00;
    HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, ACCEL_CONFIG, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);

    // 使能数据就绪中断（INT_ENABLE = 0x01 → 数据就绪时触发中断）
    data = 0x01;
    HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, INT_ENABLE, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

/**
 * @brief  读取单个通道 MPU6050 的原始数据，存入结构体
 * @param  hi2c：I2C 句柄
 * @param  channel：当前通道号（0-5）
 */
void MPU6050_ReadRawData(I2C_HandleTypeDef *hi2c, uint8_t channel)
{
    uint8_t buffer[14];  // 一次性读取 14 字节（加速度计 6 字节 + 温度 2 字节 + 陀螺仪 6 字节）

    // 从 ACCEL_XOUT_H 寄存器开始读取 14 字节数据
    HAL_I2C_Mem_Read(hi2c, MPU6050_ADDR, ACCEL_XOUT_H, I2C_MEMADD_SIZE_8BIT, buffer, 14, 100);

    // 存储加速度计原始值（16位：高字节<<8 | 低字节）
    mpu_data[channel].ax_raw = (int16_t)((buffer[0] << 8) | buffer[1]);
    mpu_data[channel].ay_raw = (int16_t)((buffer[2] << 8) | buffer[3]);
    mpu_data[channel].az_raw = (int16_t)((buffer[4] << 8) | buffer[5]);

    // 存储陀螺仪原始值（跳过温度数据：buffer[6-7]）
    mpu_data[channel].gx_raw = (int16_t)((buffer[8] << 8) | buffer[9]);
    mpu_data[channel].gy_raw = (int16_t)((buffer[10] << 8) | buffer[11]);
    mpu_data[channel].gz_raw = (int16_t)((buffer[12] << 8) | buffer[13]);
}

/**
 * @brief  将原始数据转换为物理单位（g / °/s）
 * @param  channel：当前通道号（0-5）
 */
void MPU6050_CalcPhysicalData(uint8_t channel)
{
    // 加速度计：原始值 / 量程系数 → 单位：g
    mpu_data[channel].accel_x = (float)mpu_data[channel].ax_raw / ACCEL_SCALE;
    mpu_data[channel].accel_y = (float)mpu_data[channel].ay_raw / ACCEL_SCALE;
    mpu_data[channel].accel_z = (float)mpu_data[channel].az_raw / ACCEL_SCALE;

    // 陀螺仪：原始值 / 量程系数 → 单位：°/s
    mpu_data[channel].gyro_x = (float)mpu_data[channel].gx_raw / GYRO_SCALE;
    mpu_data[channel].gyro_y = (float)mpu_data[channel].gy_raw / GYRO_SCALE;
    mpu_data[channel].gyro_z = (float)mpu_data[channel].gz_raw / GYRO_SCALE;
}

/**
 * @brief  从加速度计数据计算角度（基于重力加速度）
 * @param  channel：当前通道号（0-5）
 */
void MPU6050_CalcAccelAngle(uint8_t channel)
{
    // X轴角度（绕Y轴旋转：Pitch）：atan2(ay, sqrt(ax²+az²)) → 转换为角度
    mpu_data[channel].accel_angle_x = atan2(
        mpu_data[channel].accel_y, 
        sqrt(mpu_data[channel].accel_x * mpu_data[channel].accel_x + mpu_data[channel].accel_z * mpu_data[channel].accel_z)
    ) * 180.0f / M_PI;

    // Y轴角度（绕X轴旋转：Roll）：atan2(-ax, sqrt(ay²+az²)) → 转换为角度
    mpu_data[channel].accel_angle_y = atan2(
        -mpu_data[channel].accel_x, 
        sqrt(mpu_data[channel].accel_y * mpu_data[channel].accel_y + mpu_data[channel].accel_z * mpu_data[channel].accel_z)
    ) * 180.0f / M_PI;
}

/**
 * @brief  一阶互补滤波融合角度（陀螺仪+加速度计）
 * @param  channel：当前通道号（0-5）
 */
void MPU6050_ComplementaryFilter(uint8_t channel)
{
	
    uint32_t current_time = HAL_GetTick();
    // 计算采样时间间隔（单位：秒）→ 首次采样时 last_sample_time 为 0，强制 dt=0.01s
    float dt = (mpu_data[channel].last_sample_time == 0) ? 0.01f : (current_time - mpu_data[channel].last_sample_time) / 1000.0f;
    mpu_data[channel].last_sample_time = current_time;

    // X轴滤波角度：α*(上一次角度 + 陀螺仪角速度*dt) + (1-α)*加速度计角度
    mpu_data[channel].filter_angle_x = ALPHA * (mpu_data[channel].filter_angle_x + mpu_data[channel].gyro_y * dt) 
                                     + (1 - ALPHA) * mpu_data[channel].accel_angle_x;
    
    // Y轴滤波角度：α*(上一次角度 - 陀螺仪角速度*dt) + (1-α)*加速度计角度
    mpu_data[channel].filter_angle_y = ALPHA * (mpu_data[channel].filter_angle_y - mpu_data[channel].gyro_x * dt) 
                                     + (1 - ALPHA) * mpu_data[channel].accel_angle_y;
    
    // Z轴滤波角度：α*(上一次角度 + 陀螺仪角速度*dt) + (1-α)*0（加速度计无Z轴绝对参考）
    mpu_data[channel].filter_angle_z = ALPHA_Z * (mpu_data[channel].filter_angle_z + mpu_data[channel].gyro_z * dt) 
                                     + (1 - ALPHA_Z) * 0.0f;
	
		//加上对数据的映射***********************************************************
		mpu_data[channel].filter_angle_x = -mpu_data[channel].filter_angle_x;
		mpu_data[channel].filter_angle_y = -mpu_data[channel].filter_angle_y;
		mpu_data[channel].filter_angle_z = -mpu_data[channel].filter_angle_z;
}
//###################################################################################################################################################################

//###################################################################################################################################################################
/**
 * @brief  读取单个通道的 MPU6050 数据（完整流程：原始值→物理单位→角度→滤波）
 * @param  channel：要读取的通道号（0-5）
 */
void MPU6050_ReadSingleChannel(uint8_t channel)
{
    // 1. 切换到目标通道（调用 TCA9548A 切换通道的函数，需根据实际代码调整）
    TCA9548A_SetChannel(channel);
    HAL_Delay(10);  // 等待通道切换稳定

    // 2. 读取原始数据
    MPU6050_ReadRawData(&hi2c2, channel);  // 假设使用 hi2c2 作为 I2C 句柄

    // 3. 数据处理流程
    MPU6050_CalcPhysicalData(channel);       // 转换为物理单位
    MPU6050_CalcAccelAngle(channel);         // 加速度计计算角度
    MPU6050_ComplementaryFilter(channel);    // 互补滤波融合
}

/**
 * @brief  轮询多个通道的 MPU6050 数据
 * @param  channels：通道数组（如 {0,1,2,3,4,5}）
 * @param  channel_num：通道数量（此处为 6）
 * @param  delay_ms：通道间延时（ms）
 */
void MPU6050_PollChannels(const uint8_t channels[], uint8_t channel_num, uint16_t delay_ms , uint8_t channel_cnt)
{
//    for (uint8_t i = 0; i < channel_num; i++)
//    {
        uint8_t curr_ch = channels[channel_cnt];

        // 1. 初始化当前通道的 MPU6050（首次轮询或复位后需要，可根据需求优化）
        TCA9548A_SetChannel(curr_ch);
        HAL_Delay(10);
        MPU6050_Init(&hi2c2);
        HAL_Delay(10);

        // 2. 读取当前通道数据
        MPU6050_ReadSingleChannel(curr_ch);       				
//    }
}