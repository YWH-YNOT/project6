#ifndef TRANS_H
#define TRANS_H

#include "headfile.h"

/*
 * STM32 -> RA6M5 迁移模式开关
 * 1：按迁移方案发送固定长度 10 维特征帧
 * 0：保留旧版 1 字节命令发送链路
 */
#define GLOVE_MIGRATION_PROTOCOL_ENABLE 1U

/* 固定特征维度：4 个手指姿态传感器 x/y + 手腕 roll/pitch。 */
#define GLOVE_FEATURE_DIM            10U
/* float 转 int16 定点数的缩放倍数，例如 12.34 -> 1234。 */
#define GLOVE_FEATURE_SCALE          100.0f
/* 协议版本号。 */
#define GLOVE_FRAME_VERSION          0x01U
/* 固定帧头。 */
#define GLOVE_FRAME_SOF1             0xAAU
#define GLOVE_FRAME_SOF2             0x55U
/* 总帧长：2 + 1 + 1 + 4 + 20 + 1 + 2 = 31 字节。 */
#define GLOVE_FRAME_TOTAL_SIZE       31U

/*
 * 状态字位定义
 * bit0：数据有效
 * bit1：KEY1 按下
 * bit2：KEY2 按下
 * bit3：mode_start
 * bit4：mode1_flag
 * bit5：mode2_flag
 * bit6：mode3_flag
 *
 * 额外说明：
 * 1. RA6M5 业务层当前不会再把 bit1 / bit2 映射成离散控制命令；
 * 2. 迁移后的 0x19 只允许由“手势三”的 SVM 分类结果产生；
 * 3. 因此迁移模式下不能绕开协议，直接往 huart6 插入离散按键命令。
 */
#define GLOVE_STATUS_DATA_VALID      (1U << 0)
#define GLOVE_STATUS_KEY1_PRESSED    (1U << 1)
#define GLOVE_STATUS_KEY2_PRESSED    (1U << 2)
#define GLOVE_STATUS_MODE_START      (1U << 3)
#define GLOVE_STATUS_MODE1           (1U << 4)
#define GLOVE_STATUS_MODE2           (1U << 5)
#define GLOVE_STATUS_MODE3           (1U << 6)

uint16_t GloveFrame_Crc16(const uint8_t *data, uint16_t length);
uint16_t GloveFrame_BuildFeatureFrame(uint8_t *buffer, uint16_t buffer_size);
HAL_StatusTypeDef GloveFrame_SendFeatureFrame(void);

/*
 * 兼容旧接口名。
 * send_sensor_data() 现在发送的是迁移方案要求的 10 维特征帧，
 * 不再是旧版 21 个 float 原始传感器包。
 */
void send_sensor_data(void);

extern uint8_t tx_buffer[GLOVE_FRAME_TOTAL_SIZE];

#endif
