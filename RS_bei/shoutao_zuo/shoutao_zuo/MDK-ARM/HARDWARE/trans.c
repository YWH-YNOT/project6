#include "headfile.h"

/*
这个是串口传输数据的文件，都传的是浮点数
5个mpu6050的x轴，y轴，z轴的数据
JY901S的pitch，roll，yaw和三轴加速度

方案1，一帧包含所有浮点数，| 帧头(2字节) | 数据段(84字节)| 校验(1字节) | 帧尾(1字节) |
			
帧头：0xAA 0x55
			
数据段：21 个 float 值（每个 4 字节），顺序为：
MPU0: x → y → z
MPU1: x → y → z
...
MPU4: x → y → z
JY901S: pitch → roll → yaw → ax → ay → az

帧尾：0x0A

一共88字节，115200波特率下，1秒约1500个包
*/

// 简单和校验（8位）计算函数
// 功能：将所有数据字节相加，返回低8位作为校验值
uint8_t checksum_calculate(uint8_t *data, uint16_t length) 
{
    uint16_t sum = 0;  						// 用16位变量避免溢出
    for (uint16_t i = 0; i < length; i++) 
		{
        sum += data[i];  					// 累加所有字节
    }
    return (uint8_t)(sum & 0xFF);  // 取低8位作为校验结果
}


uint8_t tx_buffer[88];  // 缓冲区调整为88字节

void send_sensor_data(void) 
{
    uint8_t *p = tx_buffer;
    
    // 1. 帧头（2字节）
    *p++ = 0xAA;
    *p++ = 0x55;
    
    // 2. 数据段（84字节：5个MPU×12字节 + JY901S×24字节）
    for (uint8_t i = 0; i < 5; i++) 
    {
        memcpy(p, &mpu_data[i].filter_angle_x, 4); p += 4;
        memcpy(p, &mpu_data[i].filter_angle_y, 4); p += 4;
        memcpy(p, &mpu_data[i].filter_angle_z, 4); p += 4;  // 每个MPU12字节
    }
    // JY901S的6个数据（24字节）
    memcpy(p, &jy901s_data.angle_pitch, 4);   p += 4;
    memcpy(p, &jy901s_data.angle_roll,  4);   p += 4;
    memcpy(p, &jy901s_data.angle_yaw,   4);   p += 4;
    memcpy(p, &jy901s_data.acc_x, 4);       p += 4;
    memcpy(p, &jy901s_data.acc_y, 4);       p += 4;
    memcpy(p, &jy901s_data.acc_z, 4);       p += 4;
    
    // 3. 和校验（校验全部84字节数据段）
    *p++ = checksum_calculate(&tx_buffer[2], 84);  // 修正长度为84
    
    // 4. 帧尾（1字节）
    *p++ = 0x0A;
    
    
}


