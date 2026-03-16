#ifndef __BSP_IIC_H
#define __BSP_IIC_H
 
#include "headfile.h"

#define IIC_SDA_Read() 			 		 HAL_GPIO_ReadPin  (IIC_SDA_GPIO_Port,IIC_SDA_Pin) 
 
#define IIC_SDA_High() 					 HAL_GPIO_WritePin (IIC_SDA_GPIO_Port,IIC_SDA_Pin,GPIO_PIN_SET)  
#define IIC_SDA_Low() 			 		   HAL_GPIO_WritePin (IIC_SDA_GPIO_Port,IIC_SDA_Pin,GPIO_PIN_RESET)  
 
#define IIC_SCL_High()					   HAL_GPIO_WritePin (IIC_SCL_GPIO_Port,IIC_SCL_Pin,GPIO_PIN_SET)  
#define IIC_SCL_Low() 					   HAL_GPIO_WritePin (IIC_SCL_GPIO_Port,IIC_SCL_Pin,GPIO_PIN_RESET)
 
#define TCA9548A_SLAVE_ADDR   0x70      //µÿ÷∑
 
#define TCA9548A_WRITE_BIT		    0x00
#define TCA9548A_READ_BIT			0x01
 
 
#define TCA9548A_CHANNEL_0          0x01
#define TCA9548A_CHANNEL_1          0x02
#define TCA9548A_CHANNEL_2          0x04
#define TCA9548A_CHANNEL_3          0x08
#define TCA9548A_CHANNEL_4          0x10
#define TCA9548A_CHANNEL_5          0x20
#define TCA9548A_CHANNEL_6          0x40
#define TCA9548A_CHANNEL_7          0x80
 
 
extern void IIC_Start (void);
extern void IIC_Stop(void);
extern uint8_t IIC_Wait_Ack(void);
extern void TCA9548A_SetChannel(uint8_t channel);
extern uint8_t IIC_ReadByte(uint8_t ReadAck);
extern void IIC_Ack(void);
extern void IIC_NAck(void);
void TCA9548A_init(void);


 
 
#endif