/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "gpio.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "headfile.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
const uint8_t poll_channels[] = {0, 1, 2, 3}; // 通道0-3（通道4硬件故障已禁用）
#define CHANNEL_COUNT                                                          \
  (sizeof(poll_channels) / sizeof(poll_channels[0])) // 通道数量（4个）
int change_flag = 0;
uint8_t channel_cnt, control_cnt = 0;
int cnt1 = 0, cnt2 = 0;
uint8_t mode_zhang = 0;
int forward = 0, back = 0;

int allow_send_flag = 1;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC3_Init();
  MX_I2C1_Init();
  MX_UART4_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART6_UART_Init();
  MX_TIM6_Init();
  MX_I2C2_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start_IT(&htim6); // 定时器初始化
  //	HAL_UART_Receive_IT(&huart6, &uart6_data,1);		//调试串口初始化
  HAL_UART_Receive_IT(&huart1, &uart6_data, 1); // RS485初始化
  HAL_UART_Receive_IT(&huart2, &uart2_data, 1); // 陀螺仪串口初始化
  JY901S_Init();
  MPU6050_Init(&hi2c2);
  TCA9548A_init();
  TCA9548A_SetChannel(0); // 设置初始通道

  for (uint8_t i = 0; i < CHANNEL_COUNT; i++) // 初始化所有通道的 MPU6050
  {
    TCA9548A_SetChannel(poll_channels[i]); // 切换到当前通道
    HAL_Delay(10);                         // 等待通道切换稳定
    MPU6050_Init(&hi2c2);                  // 初始化当前通道的 MPU6050
    HAL_Delay(100);                        // 给传感器初始化留时间
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  static uint32_t debug_print_cnt = 0; // 调试打印计数器，控制打印频率
  channel_cnt = 0;
  while (1) {
    key_read();

    if (channel_cnt > 3) // 通道计数器若超出范围，则需要清零
    {
      channel_cnt = 0;
    }
    MPU6050_PollChannels(poll_channels, CHANNEL_COUNT, 500, channel_cnt);
    // Gesture_Control_pro();	// 测试阶段暂停手势模式识别
    //    Gesture_Move();       // 挥手方向识别（角速度状态机）

    // 统一手势识别：每 0.2s 采样一次，方向手势发出对应命令，其余发 0x00
    // HAL_UART_Transmit 已在 Gesture_Run 内部调用（每 0.2s 一次）
#if !GLOVE_MIGRATION_PROTOCOL_ENABLE
    /*
     * 旧版逻辑：
     * STM32 本地完成手势识别，再通过 huart6 发送 1 字节控制命令。
     */
    Gesture_Test();
#endif

    // 格式：f0x,f0y,f1x,f1y,f2x,f2y,f3x,f3y,roll,pitch
    //        printf("%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\r\n",
    //              mpu_data[0].filter_angle_x, mpu_data[0].filter_angle_y,
    //              mpu_data[1].filter_angle_x, mpu_data[1].filter_angle_y,
    //				mpu_data[2].filter_angle_x,
    // mpu_data[2].filter_angle_y,
    //              mpu_data[3].filter_angle_x, mpu_data[3].filter_angle_y,
    //              jy901s_data.angle_roll, jy901s_data.angle_pitch);

    /* ---- 挥手数据采集模式（采集时取消注释，正常运行时注释掉）----
     * 配合 tools/collect_swipe.py 使用
     * 格式：roll,pitch,gyro_z,gyro_y */
    //        printf("%.2f,%.2f,%.2f,%.2f\r\n",
    //               jy901s_data.angle_roll,  jy901s_data.angle_pitch,
    //               jy901s_data.gyro_z,     jy901s_data.gyro_y);

    // ---- 命令发送：通过 huart6 发送 current_cmd（1字节）----
    // Gesture_Test() 每 0.2s 更新 current_cmd，此处立即发出
    
    // ---- mode1/2/3 旧逻辑（按键触发模式，保留不变）----
//    if (trans_flag == 1 && mode1_flag == 1) // 定时发送
//    {
//      control_cnt++;
//      if (control_cnt > 3) {
//        current_cmd = CMD_ANJIAN_SHIJUE;
//        control_cnt = 0;
//      }
//    } else if (trans_flag == 1 && mode2_flag == 1) // 定时发送
//    {
//      control_cnt++;
//      if (control_cnt > 3) {
//        current_cmd = CMD_KAIGUAN_SHIJUE;
//        control_cnt = 0;
//      }
//    } else if (trans_flag == 1 && mode3_flag == 1) // 定时发送
//    {
//      control_cnt++;
//      if (control_cnt > 3) {
//        current_cmd = CMD_ANJIAN_DONGZUO;
//        control_cnt = 0;
//      }
//    }
		
#if GLOVE_MIGRATION_PROTOCOL_ENABLE
    if (trans_flag == 1) {
      trans_flag = 0;

      /*
       * 迁移模式：
       * STM32 只负责前端采集与特征发送。
       * 每到发送节拍就通过 huart6 向 RA6M5 发出一帧 10 维固定特征数据。
       */
      (void)GloveFrame_SendFeatureFrame();
    }
#else
    if (trans_flag == 1) {
      trans_flag = 0;
      
      static uint8_t history_cmd = 0x00;
      static uint8_t cmd_confirm_cnt = 0;
      
      if (current_cmd != 0x02) {
          if (current_cmd == history_cmd) {
              cmd_confirm_cnt++;
              if (cmd_confirm_cnt >= 2) {
                  HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xFFFF);
                  // 发送后可以保持上限或者清零，看需求，为了持续发送动作我们不清理，或者给个上限防止溢出
                  if (cmd_confirm_cnt > 100) cmd_confirm_cnt = 2; 
              }
          } else {
              history_cmd = current_cmd;
              cmd_confirm_cnt = 1; // 新动作出现第1次
          }
      } else {
          // 当前为 0x00(无动作或识别中)，清理历史
          history_cmd = 0x00;
          cmd_confirm_cnt = 0;
      }
    }
#endif


    // 通道计数器递增，自动循环
    channel_cnt++;

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
