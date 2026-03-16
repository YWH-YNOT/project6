#include "command.h"
#include "headfile.h"


//***********************************************************************************************
/*
调试串口函数
串口终端
每发一个字节的数据，接收数据寄存器触发串口中断
*/
uint8_t num_count1 = 0;			//串口解析变量
char uart_ts1_data[20] = "0";		//数据变量缓存
//数据格式!1.25,0.1,0.5,100.0
void uart_ts1_ctrl(uint8_t rxdata)
{
	static int uart_ts1_flag,uart_ts1_index;			//创建标志位与索引
	if(rxdata=='!'&& uart_ts1_flag==0)						//找到包头	
	{
		uart_ts1_flag=1;														//初始化标志位与索引
		num_count1=0;																//标记是第几个参数：
		uart_ts1_index=0;
	}	
	else if(uart_ts1_flag == 1)
	{
		if(rxdata == ',')
		{
			num_count1++;
			uart_ts1_data[uart_ts1_index] = '\0';
			if(num_count1 == 1)
			{
																									//第一个数据处理
			}
			else if(num_count1 == 2)
			{				
																									//第二个数据处理
			}
			else if(num_count1 == 3)
			{
																									//第三个数据处理
			}
			else if(num_count1 == 4)
			{
				
				num_count1 = 0;												//找到全部数据，计数器清零，标志位清零
				uart_ts1_flag = 0;
			}
			uart_ts1_index = 0;                  		// 索引归0，下一个参数从头存
			memset(uart_ts1_data, 0, sizeof(uart_ts1_data)); // 彻底清空
		}
		else
		{
			uart_ts1_data[uart_ts1_index] = rxdata;	//如果没有找到，继续读取数据
			uart_ts1_index++;												//索引递增
		}
		
	}
}
//******************************************************************************************