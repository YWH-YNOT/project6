#include "glove_frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * 调试打印时给 10 维特征起一个固定名字。
 * 这里沿用 STM32 端拼帧顺序，方便你两边对着看：
 * mpu0_x, mpu0_y, ... , mpu3_x, mpu3_y, roll, pitch。
 */
static char const * const g_glove_feature_names[GLOVE_FRAME_FEATURE_COUNT] =
{
    "mpu0_x",
    "mpu0_y",
    "mpu1_x",
    "mpu1_y",
    "mpu2_x",
    "mpu2_y",
    "mpu3_x",
    "mpu3_y",
    "roll",
    "pitch"
};

static uint16_t glove_frame_read_u16_le (uint8_t const * p_data)
{
    /* STM32 端按小端打包，因此这里按 little-endian 还原。 */
    return (uint16_t) (((uint16_t) p_data[1] << 8) | (uint16_t) p_data[0]);
}

static int16_t glove_frame_read_i16_le (uint8_t const * p_data)
{
    return (int16_t) glove_frame_read_u16_le(p_data);
}

static uint32_t glove_frame_read_u32_le (uint8_t const * p_data)
{
    return ((uint32_t) p_data[0]) |
           ((uint32_t) p_data[1] << 8) |
           ((uint32_t) p_data[2] << 16) |
           ((uint32_t) p_data[3] << 24);
}

static uint32_t glove_frame_write_unsigned (uint32_t value, char * p_buffer)
{
    char     temp[10];
    uint32_t count = 0U;
    uint32_t index = 0U;

    if (0U == value)
    {
        p_buffer[0] = '0';
        return 1U;
    }

    while (value > 0U)
    {
        temp[count++] = (char) ('0' + (value % 10U));
        value /= 10U;
    }

    while (count > 0U)
    {
        p_buffer[index++] = temp[--count];
    }

    return index;
}

static fsp_err_t glove_frame_send_text (mid_uart_port_t tx_port, char const * p_text)
{
    if (NULL == p_text)
    {
        return FSP_ERR_ASSERTION;
    }

    return MID_Uart_SendString(tx_port, p_text);
}

static fsp_err_t glove_frame_send_uint32 (mid_uart_port_t tx_port, uint32_t value)
{
    char     buffer[11];
    uint32_t length = glove_frame_write_unsigned(value, buffer);

    return MID_Uart_SendBytes(tx_port, (uint8_t const *) buffer, length);
}

static fsp_err_t glove_frame_send_hex_nibble (mid_uart_port_t tx_port, uint8_t value)
{
    uint8_t ascii_code = 0U;
    char    ch;

    /*
     * 这里不用三目运算符，避免不同分支在整数提升时出现有符号/无符号告警。
     * value 的有效范围本来就只有 0~15，因此直接分支转换更直观。
     */
    if (value < 10U)
    {
        ascii_code = (uint8_t) ('0' + value);
    }
    else
    {
        ascii_code = (uint8_t) ('A' + (value - 10U));
    }

    ch = (char) ascii_code;

    return MID_Uart_SendBytes(tx_port, (uint8_t const *) &ch, 1U);
}

static fsp_err_t glove_frame_send_hex8 (mid_uart_port_t tx_port, uint8_t value)
{
    fsp_err_t err = glove_frame_send_hex_nibble(tx_port, (uint8_t) ((value >> 4) & 0x0FU));
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return glove_frame_send_hex_nibble(tx_port, (uint8_t) (value & 0x0FU));
}

static fsp_err_t glove_frame_send_hex16 (mid_uart_port_t tx_port, uint16_t value)
{
    fsp_err_t err = glove_frame_send_hex8(tx_port, (uint8_t) ((value >> 8) & 0x00FFU));
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return glove_frame_send_hex8(tx_port, (uint8_t) (value & 0x00FFU));
}

static fsp_err_t glove_frame_send_scaled_value (mid_uart_port_t tx_port, int16_t raw_value)
{
    char     buffer[16];
    uint32_t index     = 0U;
    int32_t  abs_value = (int32_t) raw_value;

    /*
     * 串口里传的是“放大 100 倍后的 int16”。
     * 调试打印时按固定 2 位小数还原成文本，避免依赖 printf 的 %f。
     */
    if (abs_value < 0)
    {
        buffer[index++] = '-';
        abs_value       = -abs_value;
    }

    index += glove_frame_write_unsigned((uint32_t) (abs_value / 100), &buffer[index]);
    buffer[index++] = '.';
    buffer[index++] = (char) ('0' + ((abs_value / 10) % 10));
    buffer[index++] = (char) ('0' + (abs_value % 10));

    return MID_Uart_SendBytes(tx_port, (uint8_t const *) buffer, index);
}

static fsp_err_t glove_frame_send_status_flags (mid_uart_port_t tx_port, uint8_t status)
{
    bool      has_flag = false;
    fsp_err_t err      = FSP_SUCCESS;

    /* 逐位打印状态字含义，方便调试时直接看出哪些标志被置位。 */
    if (status & GLOVE_FRAME_STATUS_DATA_VALID)
    {
        err = glove_frame_send_text(tx_port, "VALID");
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        has_flag = true;
    }

    if (status & GLOVE_FRAME_STATUS_KEY1_PRESSED)
    {
        if (has_flag)
        {
            err = glove_frame_send_text(tx_port, "|");
            if (FSP_SUCCESS != err)
            {
                return err;
            }
        }

        err = glove_frame_send_text(tx_port, "KEY1");
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        has_flag = true;
    }

    if (status & GLOVE_FRAME_STATUS_KEY2_PRESSED)
    {
        if (has_flag)
        {
            err = glove_frame_send_text(tx_port, "|");
            if (FSP_SUCCESS != err)
            {
                return err;
            }
        }

        err = glove_frame_send_text(tx_port, "KEY2");
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        has_flag = true;
    }

    if (status & GLOVE_FRAME_STATUS_MODE_START)
    {
        if (has_flag)
        {
            err = glove_frame_send_text(tx_port, "|");
            if (FSP_SUCCESS != err)
            {
                return err;
            }
        }

        err = glove_frame_send_text(tx_port, "START");
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        has_flag = true;
    }

    if (status & GLOVE_FRAME_STATUS_MODE1)
    {
        if (has_flag)
        {
            err = glove_frame_send_text(tx_port, "|");
            if (FSP_SUCCESS != err)
            {
                return err;
            }
        }

        err = glove_frame_send_text(tx_port, "MODE1");
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        has_flag = true;
    }

    if (status & GLOVE_FRAME_STATUS_MODE2)
    {
        if (has_flag)
        {
            err = glove_frame_send_text(tx_port, "|");
            if (FSP_SUCCESS != err)
            {
                return err;
            }
        }

        err = glove_frame_send_text(tx_port, "MODE2");
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        has_flag = true;
    }

    if (status & GLOVE_FRAME_STATUS_MODE3)
    {
        if (has_flag)
        {
            err = glove_frame_send_text(tx_port, "|");
            if (FSP_SUCCESS != err)
            {
                return err;
            }
        }

        err = glove_frame_send_text(tx_port, "MODE3");
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        has_flag = true;
    }

    if (!has_flag)
    {
        return glove_frame_send_text(tx_port, "NONE");
    }

    return FSP_SUCCESS;
}

static uint32_t glove_frame_resync_after_invalid (uint8_t * p_packet)
{
    uint32_t start_index;

    /*
     * 一旦收到 31 字节但校验失败，不直接把整包全部丢掉，
     * 而是在当前缓存里继续查找下一个可能的帧头，减少重新同步时间。
     */
    for (start_index = 1U; start_index < GLOVE_FRAME_TOTAL_SIZE; start_index++)
    {
        if (GLOVE_FRAME_SOF1 != p_packet[start_index])
        {
            continue;
        }

        if ((start_index + 1U) < GLOVE_FRAME_TOTAL_SIZE)
        {
            if (GLOVE_FRAME_SOF2 != p_packet[start_index + 1U])
            {
                continue;
            }
        }

        memmove(p_packet, &p_packet[start_index], GLOVE_FRAME_TOTAL_SIZE - start_index);
        return GLOVE_FRAME_TOTAL_SIZE - start_index;
    }

    return 0U;
}

uint16_t GloveFrame_Crc16 (uint8_t const * p_data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;

    if (NULL == p_data)
    {
        return 0U;
    }

    /*
     * CRC16-CCITT-FALSE
     * poly = 0x1021, init = 0xFFFF, refin = false, refout = false, xorout = 0x0000
     */
    for (uint16_t i = 0U; i < length; i++)
    {
        crc ^= (uint16_t) ((uint16_t) p_data[i] << 8);

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if (0U != (crc & 0x8000U))
            {
                crc = (uint16_t) ((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

fsp_err_t GloveFrame_ParsePacket (uint8_t const * p_packet, glove_frame_t * p_frame)
{
    uint16_t received_crc;
    uint16_t calculated_crc;
    uint32_t feature_offset = 8U;

    if ((NULL == p_packet) || (NULL == p_frame))
    {
        return FSP_ERR_ASSERTION;
    }

    if ((GLOVE_FRAME_SOF1 != p_packet[0]) || (GLOVE_FRAME_SOF2 != p_packet[1]))
    {
        return FSP_ERR_INVALID_DATA;
    }

    if (GLOVE_FRAME_VERSION != p_packet[2])
    {
        return FSP_ERR_INVALID_DATA;
    }

    received_crc   = glove_frame_read_u16_le(&p_packet[GLOVE_FRAME_PAYLOAD_WITHOUT_CRC]);
    calculated_crc = GloveFrame_Crc16(p_packet, GLOVE_FRAME_PAYLOAD_WITHOUT_CRC);

    if (received_crc != calculated_crc)
    {
        return FSP_ERR_INVALID_DATA;
    }

    /* 校验通过后再写结构体，避免上层拿到半解析的数据。 */
    p_frame->ver     = p_packet[2];
    p_frame->seq     = p_packet[3];
    p_frame->tick_ms = glove_frame_read_u32_le(&p_packet[4]);
    p_frame->status  = p_packet[28];
    p_frame->crc16   = received_crc;

    for (uint32_t i = 0U; i < GLOVE_FRAME_FEATURE_COUNT; i++)
    {
        p_frame->feature_raw[i] = glove_frame_read_i16_le(&p_packet[feature_offset + (i * 2U)]);
        p_frame->feature[i]     = ((float) p_frame->feature_raw[i]) / GLOVE_FRAME_FEATURE_SCALE;
    }

    return FSP_SUCCESS;
}

fsp_err_t GloveFrame_Receive (mid_uart_port_t rx_port, glove_frame_t * p_frame)
{
    uint8_t   packet[GLOVE_FRAME_TOTAL_SIZE];
    uint32_t  cached_length = 0U;
    fsp_err_t err           = FSP_SUCCESS;

    if (NULL == p_frame)
    {
        return FSP_ERR_ASSERTION;
    }

    /*
     * 采用逐字节同步方式：
     * 1. 先在字节流中寻找 AA 55；
     * 2. 找到后继续攒够 31 字节；
     * 3. 校验失败时尝试在当前缓存内重同步；
     * 4. 直到拿到一帧完整合法数据再返回。
     */
    while (1)
    {
        uint8_t rx_byte = 0U;

        err = MID_Uart_ReceiveBytes(rx_port, &rx_byte, 1U);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        if (0U == cached_length)
        {
            if (GLOVE_FRAME_SOF1 == rx_byte)
            {
                packet[cached_length++] = rx_byte;
            }

            continue;
        }

        if (1U == cached_length)
        {
            if (GLOVE_FRAME_SOF2 == rx_byte)
            {
                packet[cached_length++] = rx_byte;
            }
            else if (GLOVE_FRAME_SOF1 == rx_byte)
            {
                packet[0]      = rx_byte;
                cached_length  = 1U;
            }
            else
            {
                cached_length = 0U;
            }

            continue;
        }

        packet[cached_length++] = rx_byte;

        if (cached_length < GLOVE_FRAME_TOTAL_SIZE)
        {
            continue;
        }

        err = GloveFrame_ParsePacket(packet, p_frame);
        if (FSP_SUCCESS == err)
        {
            return FSP_SUCCESS;
        }

        cached_length = glove_frame_resync_after_invalid(packet);
    }
}

fsp_err_t GloveFrame_PrintDebug (mid_uart_port_t tx_port, glove_frame_t const * p_frame)
{
    fsp_err_t err = FSP_SUCCESS;

    if (NULL == p_frame)
    {
        return FSP_ERR_ASSERTION;
    }

    err = glove_frame_send_text(tx_port, "frame seq=");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = glove_frame_send_uint32(tx_port, p_frame->seq);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = glove_frame_send_text(tx_port, " ver=");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = glove_frame_send_uint32(tx_port, p_frame->ver);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = glove_frame_send_text(tx_port, " tick=");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = glove_frame_send_uint32(tx_port, p_frame->tick_ms);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = glove_frame_send_text(tx_port, " status=0x");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = glove_frame_send_hex8(tx_port, p_frame->status);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = glove_frame_send_text(tx_port, " crc=0x");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = glove_frame_send_hex16(tx_port, p_frame->crc16);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = glove_frame_send_text(tx_port, "\r\n");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    for (uint32_t i = 0U; i < GLOVE_FRAME_FEATURE_COUNT; i++)
    {
        err = glove_frame_send_text(tx_port, g_glove_feature_names[i]);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        err = glove_frame_send_text(tx_port, "=");
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        err = glove_frame_send_scaled_value(tx_port, p_frame->feature_raw[i]);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        if ((i % 2U) == 0U)
        {
            err = glove_frame_send_text(tx_port, "  ");
        }
        else
        {
            err = glove_frame_send_text(tx_port, "\r\n");
        }

        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    err = glove_frame_send_text(tx_port, "flags=");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = glove_frame_send_status_flags(tx_port, p_frame->status);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return glove_frame_send_text(tx_port, "\r\n\r\n");
}
