#include "trans.h"

#include <limits.h>

/*
 * 迁移阶段 STM32 端只负责：
 * 1. 继续完成多路 MPU6050 + JY901S 数据采集
 * 2. 复用原来的 10 维特征定义
 * 3. 通过固定长度二进制帧发送给 RA6M5
 *
 * 特征顺序严格对应迁移方案：
 * finger1_x, finger1_y,
 * finger2_x, finger2_y,
 * finger3_x, finger3_y,
 * finger4_x, finger4_y,
 * roll, pitch
 */

uint8_t tx_buffer[GLOVE_FRAME_TOTAL_SIZE];

/* 帧序号由发送层内部维护，接收端可据此检测丢帧。 */
static uint8_t g_glove_frame_seq = 0U;

static void glove_pack_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void glove_pack_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
    buffer[2] = (uint8_t)((value >> 16) & 0xFFU);
    buffer[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static int16_t glove_float_to_fixed(float value)
{
    float scaled = value * GLOVE_FEATURE_SCALE;

    if (scaled > (float)INT16_MAX)
    {
        scaled = (float)INT16_MAX;
    }
    else if (scaled < (float)INT16_MIN)
    {
        scaled = (float)INT16_MIN;
    }

    if (scaled >= 0.0f)
    {
        scaled += 0.5f;
    }
    else
    {
        scaled -= 0.5f;
    }

    return (int16_t)scaled;
}

static void glove_collect_feature(float feature[GLOVE_FEATURE_DIM])
{
    /*
     * 直接复用 STM32 原本给 Gesture_Classify() 使用的 10 维输入，
     * 这样 RA6M5 端后续迁移 SVM 时可以无缝对接。
     */
    feature[0] = mpu_data[0].filter_angle_x;
    feature[1] = mpu_data[0].filter_angle_y;
    feature[2] = mpu_data[1].filter_angle_x;
    feature[3] = mpu_data[1].filter_angle_y;
    feature[4] = mpu_data[2].filter_angle_x;
    feature[5] = mpu_data[2].filter_angle_y;
    feature[6] = mpu_data[3].filter_angle_x;
    feature[7] = mpu_data[3].filter_angle_y;
    feature[8] = jy901s_data.angle_roll;
    feature[9] = jy901s_data.angle_pitch;
}

static uint8_t glove_build_status(void)
{
    uint8_t status = GLOVE_STATUS_DATA_VALID;

    if (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_RESET)
    {
        status |= GLOVE_STATUS_KEY1_PRESSED;
    }

    if (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET)
    {
        status |= GLOVE_STATUS_KEY2_PRESSED;
    }

    if (mode_start)
    {
        status |= GLOVE_STATUS_MODE_START;
    }

    if (mode1_flag)
    {
        status |= GLOVE_STATUS_MODE1;
    }

    if (mode2_flag)
    {
        status |= GLOVE_STATUS_MODE2;
    }

    if (mode3_flag)
    {
        status |= GLOVE_STATUS_MODE3;
    }

    return status;
}

uint16_t GloveFrame_Crc16(const uint8_t *data, uint16_t length)
{
    /*
     * CRC16-CCITT-FALSE
     * poly = 0x1021, init = 0xFFFF
     * RA6M5 端使用同样算法即可完成校验。
     */
    uint16_t crc = 0xFFFFU;

    if (data == NULL)
    {
        return 0U;
    }

    for (uint16_t i = 0U; i < length; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

uint16_t GloveFrame_BuildFeatureFrame(uint8_t *buffer, uint16_t buffer_size)
{
    float    feature[GLOVE_FEATURE_DIM];
    uint16_t index = 0U;
    uint16_t crc16;

    if ((buffer == NULL) || (buffer_size < GLOVE_FRAME_TOTAL_SIZE))
    {
        return 0U;
    }

    glove_collect_feature(feature);

    buffer[index++] = GLOVE_FRAME_SOF1;
    buffer[index++] = GLOVE_FRAME_SOF2;
    buffer[index++] = GLOVE_FRAME_VERSION;
    buffer[index++] = g_glove_frame_seq++;

    glove_pack_u32_le(&buffer[index], HAL_GetTick());
    index += 4U;

    for (uint8_t i = 0U; i < GLOVE_FEATURE_DIM; i++)
    {
        /*
         * 发送时把 float 压缩成 int16 定点数，减少带宽占用，
         * 同时保留足够的姿态精度。
         */
        int16_t fixed_value = glove_float_to_fixed(feature[i]);
        glove_pack_u16_le(&buffer[index], (uint16_t)fixed_value);
        index += 2U;
    }

    buffer[index++] = glove_build_status();

    /*
     * CRC 覆盖范围：
     * 从 sof1 开始，到 status 结束。
     * 接收端取前 29 字节做同样 CRC，再与最后 2 字节比对。
     */
    crc16 = GloveFrame_Crc16(buffer, index);
    glove_pack_u16_le(&buffer[index], crc16);
    index += 2U;

    return index;
}

HAL_StatusTypeDef GloveFrame_SendFeatureFrame(void)
{
    uint16_t frame_length = GloveFrame_BuildFeatureFrame(tx_buffer, sizeof(tx_buffer));

    if (frame_length != GLOVE_FRAME_TOTAL_SIZE)
    {
        return HAL_ERROR;
    }

    /*
     * 当前阶段优先保证链路稳定，因此使用阻塞式串口发送。
     * 单帧仅 31 字节，在 115200 波特率下开销很小。
     */
    return HAL_UART_Transmit(&huart6, tx_buffer, frame_length, 100U);
}

void send_sensor_data(void)
{
    (void)GloveFrame_SendFeatureFrame();
}
