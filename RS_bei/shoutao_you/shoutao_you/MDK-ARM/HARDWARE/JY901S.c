#include "JY901S.h"

// 定义协议相关常量
#define FRAME_LENGTH 11  // WIT协议单帧数据长度
#define HEADER 0x55      // 帧头标识

// 接收缓冲区和相关变量
static uint8_t rx_buffer[FRAME_LENGTH] = {0};
static uint8_t buffer_index = 0;

// 全局变量存储传感器数据，供外部访问
JY901S_Data jy901s_data = {0};

// 计算校验和
static uint8_t calculate_checksum(uint8_t *data, uint8_t length) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < length; i++) {
        sum += data[i];
    }
    return sum;
}

// 16位数据组合（处理有符号数）
static int16_t combine_bytes(uint8_t high_byte, uint8_t low_byte) {
    return (int16_t)((int16_t)high_byte << 8 | low_byte);
}

// 初始化函数
void JY901S_Init(void) {
    // 重置数据结构体
    jy901s_data.acc_x = 0.0f;
    jy901s_data.acc_y = 0.0f;
    jy901s_data.acc_z = 0.0f;
    jy901s_data.gyro_x = 0.0f;
    jy901s_data.gyro_y = 0.0f;
    jy901s_data.gyro_z = 0.0f;
    jy901s_data.angle_roll = 0.0f;
    jy901s_data.angle_pitch = 0.0f;
    jy901s_data.angle_yaw = 0.0f;
    jy901s_data.temperature = 0.0f;
    jy901s_data.version = 0;
    
    // 重置标志位
    jy901s_data.acc_updated = false;
    jy901s_data.gyro_updated = false;
    jy901s_data.angle_updated = false;
    jy901s_data.temp_updated = false;
    
    // 重置缓冲区
    buffer_index = 0;
    for (uint8_t i = 0; i < FRAME_LENGTH; i++) {
        rx_buffer[i] = 0;
    }
}

// 解析JY901S传感器数据的主函数
void get_JY901S(uint8_t uart2_data) {
    // 1. 填充接收缓冲区
    rx_buffer[buffer_index++] = uart2_data;
    
    // 2. 检查帧头 (0x55)
    if (buffer_index == 1 && rx_buffer[0] != HEADER) {
        buffer_index = 0;  // 不是帧头，重置缓冲区
        return;
    }
    
    // 3. 接收到完整一帧数据（11字节）
    if (buffer_index >= FRAME_LENGTH) {
        // 4. 校验和验证
        uint8_t checksum = calculate_checksum(rx_buffer, FRAME_LENGTH - 1);
        if (checksum != rx_buffer[FRAME_LENGTH - 1]) {
            buffer_index = 0;  // 校验失败，重置缓冲区
            return;
        }
        
        // 5. 根据数据类型(TYPE)解析数据
        uint8_t type = rx_buffer[1];
        
        // 重置所有更新标志
        jy901s_data.acc_updated = false;
        jy901s_data.gyro_updated = false;
        jy901s_data.angle_updated = false;
        jy901s_data.temp_updated = false;
        
        switch (type) {
            // 加速度数据 (TYPE = 0x51)
            case 0x51:
                jy901s_data.acc_x = (float)combine_bytes(rx_buffer[3], rx_buffer[2]) / 32768.0f * 16.0f * 9.8f;
                jy901s_data.acc_y = (float)combine_bytes(rx_buffer[5], rx_buffer[4]) / 32768.0f * 16.0f * 9.8f;
                jy901s_data.acc_z = (float)combine_bytes(rx_buffer[7], rx_buffer[6]) / 32768.0f * 16.0f * 9.8f;
                jy901s_data.temperature = (float)combine_bytes(rx_buffer[9], rx_buffer[8]) / 100.0f;
                jy901s_data.acc_updated = true;
                jy901s_data.temp_updated = true;
                break;
                
            // 角速度数据 (TYPE = 0x52)
            case 0x52:
                jy901s_data.gyro_x = (float)combine_bytes(rx_buffer[3], rx_buffer[2]) / 32768.0f * 2000.0f;
                jy901s_data.gyro_y = (float)combine_bytes(rx_buffer[5], rx_buffer[4]) / 32768.0f * 2000.0f;
                jy901s_data.gyro_z = (float)combine_bytes(rx_buffer[7], rx_buffer[6]) / 32768.0f * 2000.0f;
                jy901s_data.temperature = (float)combine_bytes(rx_buffer[9], rx_buffer[8]) / 100.0f;
                jy901s_data.gyro_updated = true;
                jy901s_data.temp_updated = true;
                break;
                
            // 角度数据 (TYPE = 0x53)
            case 0x53:
                jy901s_data.angle_roll = (float)combine_bytes(rx_buffer[3], rx_buffer[2]) / 32768.0f * 180.0f;
                jy901s_data.angle_pitch = (float)combine_bytes(rx_buffer[5], rx_buffer[4]) / 32768.0f * 180.0f;
                jy901s_data.angle_yaw = (float)combine_bytes(rx_buffer[7], rx_buffer[6]) / 32768.0f * 180.0f;
                jy901s_data.version = (uint16_t)(combine_bytes(rx_buffer[9], rx_buffer[8]) & 0xFFFF);
                jy901s_data.angle_updated = true;
                break;
                
            // 其他类型数据可在此处扩展
            default:
                // 不处理的数据包类型
                break;
        }
        
        // 6. 重置缓冲区，准备接收下一帧
        buffer_index = 0;
    }
}
   
//*********************************************************************************
//切换陀螺仪波特率函数
#include "stm32f4xx_hal.h"

// 指令定义
#define UNLOCK_CMD      {0xFF, 0xAA, 0x69, 0x88, 0xB5}  // 解锁指令
#define BAUD_115200_CMD {0xFF, 0xAA, 0x04, 0x06, 0x00}  // 115200波特率指令
#define SAVE_CMD        {0xFF, 0xAA, 0x00, 0x00, 0x00}  // 保存指令

/**
 * @brief  修改JY901S传感器波特率为115200
 * @param  huart: 与传感器连接的串口句柄指针
 * @param  originalBaud: 传感器当前的波特率（默认通常为9600）
 * @retval HAL_StatusTypeDef: 操作结果（HAL_OK表示成功）
 */
HAL_StatusTypeDef JY901S_SetBaudrateTo115200(UART_HandleTypeDef *huart, uint32_t originalBaud)
{
    // 1. 初始化串口为原始波特率
    huart->Init.BaudRate = originalBaud;
    if (HAL_UART_Init(huart) != HAL_OK)
    {
        return HAL_ERROR;  // 串口初始化失败
    }

    // 2. 发送解锁指令
    uint8_t unlockCmd[] = UNLOCK_CMD;
    if (HAL_UART_Transmit(huart, unlockCmd, sizeof(unlockCmd), 1000) != HAL_OK)
    {
        return HAL_ERROR;  // 解锁指令发送失败
    }
    HAL_Delay(200);  // 等待传感器响应

    // 3. 发送修改波特率指令（115200）
    uint8_t baudCmd[] = BAUD_115200_CMD;
    if (HAL_UART_Transmit(huart, baudCmd, sizeof(baudCmd), 1000) != HAL_OK)
    {
        return HAL_ERROR;  // 波特率指令发送失败
    }

    // 4. 切换MCU串口波特率为115200
    huart->Init.BaudRate = 115200;
    if (HAL_UART_Init(huart) != HAL_OK)
    {
        return HAL_ERROR;  // 波特率切换失败
    }

    // 5. 用新波特率重新解锁
    if (HAL_UART_Transmit(huart, unlockCmd, sizeof(unlockCmd), 1000) != HAL_OK)
    {
        return HAL_ERROR;  // 新波特率下解锁失败
    }
    HAL_Delay(200);  // 等待响应

    // 6. 发送保存指令
    uint8_t saveCmd[] = SAVE_CMD;
    if (HAL_UART_Transmit(huart, saveCmd, sizeof(saveCmd), 1000) != HAL_OK)
    {
        return HAL_ERROR;  // 保存指令发送失败
    }

    return HAL_OK;  // 全部操作成功
}
/**
 * @brief  JY901S加速度校准函数
 * @note   校准前请确保模块**正面放置**（反面放置会导致数据异常）
 * @param  huart: 连接JY901S的串口句柄（如&huart2）
 * @return 0: 校准成功, 1: 校准失败
 */
uint8_t JY901S_CalibrateAcceleration(UART_HandleTypeDef *huart)
{
    // 校准指令定义（按流程顺序）
    uint8_t unlock_cmd[]    = {0xFF, 0xAA, 0x69, 0x88, 0xB5};  // 解锁指令
    uint8_t start_cal_cmd[] = {0xFF, 0xAA, 0x01, 0x01, 0x00};  // 开始校准指令
    uint8_t exit_cal_cmd[]  = {0xFF, 0xAA, 0x01, 0x00, 0x00};  // 退出校准指令
    uint8_t save_cmd[]      = {0xFF, 0xAA, 0x00, 0x00, 0x00};  // 保存校准指令
    
    // 步骤1: 发送解锁指令
    if (HAL_UART_Transmit(huart, unlock_cmd, sizeof(unlock_cmd), 100) != HAL_OK) {
        return 1;  // 发送失败返回错误
    }
    
    // 步骤1.1: 延时200ms
    HAL_Delay(200);
    
    // 步骤2: 发送开始校准指令
    if (HAL_UART_Transmit(huart, start_cal_cmd, sizeof(start_cal_cmd), 100) != HAL_OK) {
        return 1;
    }
    
    // 步骤3: 延时4秒（校准过程需要时间）
    HAL_Delay(4000);  // 4秒校准等待
    
    // 步骤3.1: 发送退出校准指令
    if (HAL_UART_Transmit(huart, exit_cal_cmd, sizeof(exit_cal_cmd), 100) != HAL_OK) {
        return 1;
    }
    
    // 延时100ms
    HAL_Delay(100);
    
    // 步骤4: 发送保存指令（将校准参数写入模块）
    if (HAL_UART_Transmit(huart, save_cmd, sizeof(save_cmd), 100) != HAL_OK) {
        return 1;
    }
    
    // 校准完成提示
    HAL_Delay(500);  // 等待保存完成
		printf("ok\n");
    return 0;  // 校准成功
}
