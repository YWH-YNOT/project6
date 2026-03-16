# STM32 主函数逻辑与函数调用链说明

> 芯片：STM32F407 · 架构：裸机 + 前后台系统 · 编译工具：Keil MDK

---

## 一、整体执行模型

| 层次 | 位置 | 说明 |
|------|------|------|
| **后台（Background）** | `main()` 中的 `while(1)` | 轮询传感器、运行手势识别 |
| **前台（Foreground）** | 各硬件中断（ISR） | 异步接收串口数据、定时器计数 |

---

## 二、启动阶段（`main()` 初始化流程）

```
上电复位
  └─► startup_stm32f407xx.s   （汇编启动文件）
        └─► main()
              ├─ HAL_Init()                // SysTick 1ms 节拍初始化
              ├─ SystemClock_Config()      // PLL：168 MHz
              │
              ├── 外设初始化（CubeMX 自动生成）
              │    ├─ MX_GPIO_Init()       // GPIO 方向/复用配置
              │    ├─ MX_DMA_Init()        // DMA 控制器
              │    ├─ MX_ADC3_Init()       // ADC3（预留，当前未用）
              │    ├─ MX_I2C1_Init()       // I²C1（未用）
              │    ├─ MX_I2C2_Init()       // I²C2 → TCA9548A + MPU6050
              │    ├─ MX_UART4_Init()      // UART4 115200（调试预留）
              │    ├─ MX_USART1_UART_Init()// USART1 115200（RS485）
              │    ├─ MX_USART2_UART_Init()// USART2 115200（JY901S）
              │    ├─ MX_USART6_UART_Init()// USART6 115200（命令输出，DMA）
              │    └─ MX_TIM6_Init()       // TIM6：84MHz/(83+1)/(999+1) = 1kHz = 1ms
              │
              └── 用户外设初始化
                   ├─ HAL_TIM_Base_Start_IT(&htim6)      // 启动 TIM6 1ms 中断
                   ├─ HAL_UART_Receive_IT(&huart1, ...)  // RS485 接收中断
                   ├─ HAL_UART_Receive_IT(&huart2, ...)  // JY901S 接收中断
                   ├─ JY901S_Init()                      // 腕部陀螺仪初始化
                   ├─ MPU6050_Init(&hi2c2)               // 主通道初始化
                   ├─ TCA9548A_init()                    // I²C 多路复用器
                   └─ for(i=0; i<4; i++)                 // 逐一初始化4路手指MPU6050
                         TCA9548A_SetChannel(ch[i])
                         MPU6050_Init(&hi2c2)
```

---

## 三、主循环逐步解析（`while(1)`）

```c
channel_cnt = 0;   // 通道计数器，0~3 轮询4个MPU6050

while(1) {

  // ① 按键读取（模式切换）
  key_read();

  // ② 通道计数器越界保护
  if (channel_cnt > 3) channel_cnt = 0;

  // ③ 轮询当前通道的 MPU6050（每次只读一路手指）
  MPU6050_PollChannels(poll_channels, 4, 500, channel_cnt);

  // ④ 手势识别（核心，每200ms推理一次）
  Gesture_Test();   // 实际调用 Gesture_Run()

  // ⑤ 旧版按键工作模式（保留兼容）
  if (trans_flag==1 && mode1_flag==1) { current_cmd = 0x67; }  // 按键视觉
  if (trans_flag==1 && mode2_flag==1) { current_cmd = 0x68; }  // 开关视觉
  if (trans_flag==1 && mode3_flag==1) { current_cmd = 0x05; }  // 按键动作

  // ⑥ 通道计数器递增（下轮切换到下一个手指）
  channel_cnt++;
}
```

> **关键设计**：每轮主循环只读一路 MPU6050（`channel_cnt` 0→1→2→3→0），四轮后凑齐所有手指数据，避免等待多次 I²C 传输造成阻塞。

---

## 四、核心函数调用链

### 4.1 传感器数据读取链

```
MPU6050_PollChannels(channels[], 4, 500, channel_cnt)
  ├─ TCA9548A_SetChannel(channels[channel_cnt])  // 切换到当前手指通道
  └─ MPU6050_ReadSingleChannel(channel_cnt)
       ├─ MPU6050_ReadRawData(&hi2c2, ch)         // I²C 读 16 位原始值
       ├─ MPU6050_CalcPhysicalData(ch)            // 换算为 g 和 °/s
       ├─ MPU6050_CalcAccelAngle(ch)              // arctan 计算加速度计角度
       ├─ MPU6050_ComplementaryFilter(ch)         // 互补滤波 → filter_angle_x/y
       │   // filter_angle = 0.95*(上帧+gyro*dt) + 0.05*accel_angle
       └─ MPU6050_CalcMotionAndVelocity(ch)       // 速度积分（备用）
```

> **JY901S 腕部数据**通过 USART2 中断独立更新，不在主循环内轮询：
> `USART2_IRQHandler` → 解析 → `jy901s_data.angle_roll / pitch / gyro_z / gyro_y`

---

### 4.2 手势识别链

```
Gesture_Test()
  └─ Gesture_Run()
        ├─ gr_tick++;  if(gr_tick < 5) return;     // 限频：每5帧执行一次 ≈ 200ms
        │
        ├─ Gesture_Classify(                        // SVM 推理（gesture_model.h）
        │     mpu_data[0].filter_angle_x, _y,       // 手指0 (食指根部)
        │     mpu_data[1].filter_angle_x, _y,       // 手指1
        │     mpu_data[2].filter_angle_x, _y,       // 手指2
        │     mpu_data[3].filter_angle_x, _y,       // 手指3
        │     jy901s_data.angle_roll,               // 腕部 roll
        │     jy901s_data.angle_pitch )             // 腕部 pitch
        │     → 返回 label (0~8)
        │
        ├─ 命令映射表 gr_cmd_map[label] → current_cmd
        │
        ├─ printf("[手势] %d - %s (0x%02X)")        // 调试输出 → USART6
        │
        └─ HAL_UART_Transmit(&huart6, &current_cmd, 1, 0xFFFF)
                                                    // 每200ms发1字节给下位机
```

**9类手势命令映射：**

| Label | 手势 | 命令字节 |
|-------|------|---------|
| 0 | `fist` 握拳 | `0x01` |
| 1 | `open` 张开 | `0x02` |
| 2 | `one` 比1 | `0x03` |
| 3 | `two` 比2/V | `0x04` |
| 4 | `rock` 摇滚 | `0x05` |
| 5 | `up` ↑ | `0x08` |
| 6 | `down` ↓ | `0x0A` |
| 7 | `left` ← | `0x06` |
| 8 | `right` → | `0x07` |

---

### 4.3 `printf` 重定向

`usart.c` 末尾重定义了标准输出，**所有 `printf` 都走 USART6**：

```c
int fputc(int ch, FILE *f) {
    HAL_UART_Transmit(&huart6, (uint8_t*)&ch, 1, 0xffff);
    return ch;
}
```

> ⚠️ 调试打印文本 和 命令字节 共用同一串口（USART6），下位机需注意区分。

---

## 五、中断系统（与主循环并行运行）

| 中断源 | 处理函数 | 优先级 | 作用 |
|--------|---------|--------|------|
| `SysTick` 1ms | `SysTick_Handler` → `HAL_IncTick()` | 最高 | HAL 时基，`HAL_Delay()` 依赖 |
| `TIM6` 1ms | `TIM6_DAC_IRQHandler` | 0 | 节拍计数，`trans_flag` 旧逻辑计时 |
| `USART1` | `USART1_IRQHandler` | 1 | RS485 逐字节接收命令 |
| `USART2` | `USART2_IRQHandler` | 3 | JY901S 数据流接收，解析腕部角度 |
| `USART6` | `USART6_IRQHandler` | 1 | 命令发送完成回调（DMA） |
| `DMA2 Stream6` | `DMA2_Stream6_IRQHandler` | - | USART6 TX DMA 完成通知 |

---

## 六、全局数据流总览

```
JY901S ──USART2─IRQ──► jy901s_data { roll, pitch, gyro_z, gyro_y }
                                                │
MPU6050×4 ──I²C──TCA9548A──► mpu_data[0-3] { filter_angle_x, filter_angle_y }
  （主循环轮询，channel_cnt: 0→1→2→3→0）          │
                                                │
                               ┌────────────────▼────────────────┐
                               │  Gesture_Run()  每200ms执行      │
                               │  Gesture_Classify() SVM 10维推理 │
                               └────────────────┬────────────────┘
                                                │ current_cmd（1字节）
                               ┌────────────────▼────────────────┐
                               │  USART6 TX（HAL_UART_Transmit） │──► 下位机（机器人/小车）
                               └─────────────────────────────────┘
```

---

## 七、设计要点总结

| # | 要点 | 说明 |
|---|------|------|
| 1 | **分时轮询传感器** | 每轮只读一路MPU6050，避免I²C阻塞整个循环 |
| 2 | **手势识别限频** | `gr_tick` 计数器限制推理频率为5帧/次（~200ms），降低CPU压力 |
| 3 | **腕部数据异步更新** | JY901S 走中断更新，与MPU6050主循环完全解耦 |
| 4 | **命令发送内置于识别函数** | `Gesture_Run()` 内部直接调用 `HAL_UART_Transmit`，每200ms自动发一次 |
| 5 | **printf与命令共用USART6** | 调试打印和控制命令走同一串口，生产环境建议禁用printf |
