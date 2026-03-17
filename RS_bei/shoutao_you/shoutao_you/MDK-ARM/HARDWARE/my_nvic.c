#ifndef my_nvic_H_
#define my_nvic_H_
#include "headfile.h"

//变量定义
uint8_t uart6_data = 0;
uint8_t uart2_data = 0;
// ===================== 上位机协议定义（3字节定长） =====================
// 协议格式： [0]=0x03(长度) [1]=0x03(地址) [2]=命令(上位机发0x00请求/或其他)
#define PC_PROTOCOL_LENGTH 3
#define PC_FRAME_LEN_BYTE  0x03
#define PC_FRAME_ADDR_BYTE 0x03

uint8_t uart6_pc_rx_buffer[PC_PROTOCOL_LENGTH]; //存完整一帧
uint8_t uart6_pc_rx_index = 0;									//当前接收到第几个字节
volatile uint8_t uart6_pc_rx_flag = 0;   				// 收满一帧置1

// ===================== UART6 回环调试开关 =====================
// 1：把收到的每个字节原样回发（仅用于调试）
// 0：关闭回环（正式协议用）
#define UART6_ECHO_DEBUG  0

// ======================================================================
// 1) 上位机协议回复函数：回复当前手势命令
// ======================================================================
void PC_ReplyCommand(void)
{
    uint8_t reply_frame[PC_PROTOCOL_LENGTH];
    uint8_t cmd_byte;

    reply_frame[0] = PC_FRAME_LEN_BYTE;   // 0x03
    reply_frame[1] = PC_FRAME_ADDR_BYTE;  // 0x03

    if (current_cmd == CMD_NONE)
		{
        cmd_byte = 0x00;
		}
		else
		{
        cmd_byte = current_cmd;
		}

    reply_frame[2] = cmd_byte;

    // 发送 3 字节回复
//    HAL_UART_Transmit(&huart6, reply_frame, PC_PROTOCOL_LENGTH, 100);
//		HAL_Delay(10);
		HAL_UART_Transmit(&huart1, reply_frame, PC_PROTOCOL_LENGTH, 100);

}

// ======================================================================
// 2) UART6 上位机协议：逐字节解析（小状态机）
//    按你想要的优化风格：回调里先挂下一次接收，再调用这里处理当前字节
// ======================================================================
static inline void UART6_PC_Parse_Byte(uint8_t byte)
{
    // 状态0：等待帧头（长度 0x03）
    if (uart6_pc_rx_index == 0)
    {
        if (byte == PC_FRAME_LEN_BYTE)
        {
            uart6_pc_rx_buffer[0] = byte;
            uart6_pc_rx_index = 1;
        }
        // else: 丢弃，继续等待帧头
        return;
    }

    // 状态1/2：收剩余字节
    uart6_pc_rx_buffer[uart6_pc_rx_index++] = byte;

    // 收满 3 字节
    if (uart6_pc_rx_index >= PC_PROTOCOL_LENGTH)
    {
        uart6_pc_rx_index = 0;      // 立刻复位，准备下一帧
        uart6_pc_rx_flag = 1;

        // 校验帧格式：[0]=0x03 [1]=0x03
        if (uart6_pc_rx_buffer[0] == PC_FRAME_LEN_BYTE &&
            uart6_pc_rx_buffer[1] == PC_FRAME_ADDR_BYTE &&
						uart6_pc_rx_buffer[2] == 0X00 )
        {
            // 协议正确：回复上位机（回当前手势命令）
            PC_ReplyCommand();
        }

				//清空变量
         memset(uart6_pc_rx_buffer, 0, PC_PROTOCOL_LENGTH);
    }
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{	
//	if(huart == &huart6)
//	{
//        HAL_UART_Receive_IT(&huart6, &uart6_data, 1);

//        // （可选）回环调试：原样回传收到的字节
//#if UART6_ECHO_DEBUG
//        HAL_UART_Transmit(&huart6, &uart6_data, 1, 0xffff);
//#endif

//        // 再解析当前字节（组帧+校验+回复）
//        UART6_PC_Parse_Byte(uart6_data);
//	}
	if(huart == &huart1)
	{
        HAL_UART_Receive_IT(&huart1, &uart6_data, 1);

        // （可选）回环调试：原样回传收到的字节
#if UART6_ECHO_DEBUG
        HAL_UART_Transmit(&huart6, &uart6_data, 1, 0xffff);
#endif

        // 再解析当前字节（组帧+校验+回复）
        UART6_PC_Parse_Byte(uart6_data);
	}
	if (huart == &huart2) 
	{
    get_JY901S(uart2_data);
    HAL_UART_Receive_IT(&huart2, &uart2_data, 1);
  }
	
}





/* 全局计数器和标志变量 */
uint32_t task1ms_cnt = 0;    // 1ms任务计数器
uint32_t task1s_cnt = 0;     // 1s任务计数器


uint32_t task200ms_cnt = 0;    // 1ms任务计数器
/* 任务执行标志（可被主循环检测） */
volatile bool task1ms_flag = false;

volatile bool task200ms_flag = false;

volatile bool task1s_flag = false;

int t1_flag = 0;
volatile int move_flag = false;
volatile bool direction_lock_flag = true; // 初始为true，表示可以检测方向

volatile bool trans_flag = true; // 初始为true，表示可以检测方向
/* 定时器中断回调函数（1ms触发一次） */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) 
{
    if (htim->Instance == TIM6) 
		{
				
        /* 1ms任务（每个中断周期执行） */
        task1ms_flag = true;			
	
        if (++task200ms_cnt >= 20) 
				{
						trans_flag = 1;					
            task200ms_cnt = 0;
            task200ms_flag = true;
					
        }
        /* 1s任务（每1000ms执行一次） */
        if (++task1s_cnt >= 1000) 
				{
						direction_lock_flag = true; // 1秒后重置标志位，允许再次检测方向
            task1s_cnt = 0;
            task1s_flag = true;
					
        }
				
    }
}

void key_read(void)
{
    if(HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == 0)
    {
        HAL_Delay(10);
        if(HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == 0)
        {
            current_cmd = 0x15;
#if !GLOVE_MIGRATION_PROTOCOL_ENABLE
            /*
             * 旧版模式下，按键事件会直接通过 huart6 发出 1 字节命令。
             * 迁移模式下必须关闭这类离散发送，避免破坏固定长度二进制帧流。
             */
            HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xFFFF);
#endif
            while(HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == 0);
        }
    }
    if(HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == 0)
    {
        HAL_Delay(10);
        if(HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == 0)
        {
            current_cmd = 0x16;
#if !GLOVE_MIGRATION_PROTOCOL_ENABLE
            HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xFFFF);
#endif
            while(HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == 0);
        }
    }
}
#endif 


