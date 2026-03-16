#include "BSP_IIC.h"
#include "headfile.h"

void TCA9548A_init(void)
{
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_8, GPIO_PIN_RESET); 		//A0
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_9, GPIO_PIN_RESET);			//A1
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_15, GPIO_PIN_RESET);		//A2
}

void TCA9548A_SetChannel(uint8_t channel)
{		
    uint8_t data;
    switch(channel)
    {
        case 0:
            data = TCA9548A_CHANNEL_0;
            break;
        case 1:
            data = TCA9548A_CHANNEL_1;
            break;
        case 2:
            data = TCA9548A_CHANNEL_2;
            break;
        case 3:
            data = TCA9548A_CHANNEL_3;
            break;
        case 4:
            data = TCA9548A_CHANNEL_4;
            break;
        case 5:
            data = TCA9548A_CHANNEL_5;
            break;
        case 6:
            data = TCA9548A_CHANNEL_6;
            break;
        case 7:
            data = TCA9548A_CHANNEL_7;
            break;
        default:
            break;        
    }
 
    HAL_I2C_Master_Transmit(&hi2c2, (TCA9548A_SLAVE_ADDR << 1) , &data, 1, 10);
}









