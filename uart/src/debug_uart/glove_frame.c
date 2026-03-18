#include "glove_frame.h"

#include <stddef.h>
#include <string.h>

/*
 * 内部函数作用：
 *   按小端格式读取 16 位无符号整数。
 * 调用关系：
 *   仅供本文件内部解包函数使用。
 * 参数说明：
 *   p_data: 指向 2 字节源数据。
 * 返回值：
 *   还原后的 uint16_t 数值。
 */
static uint16_t glove_frame_read_u16_le (uint8_t const * p_data)
{
    return (uint16_t) (((uint16_t) p_data[1] << 8) | (uint16_t) p_data[0]);
}

/*
 * 内部函数作用：
 *   按小端格式读取 16 位有符号整数。
 * 参数说明：
 *   p_data: 指向 2 字节源数据。
 * 返回值：
 *   还原后的 int16_t 数值。
 */
static int16_t glove_frame_read_i16_le (uint8_t const * p_data)
{
    return (int16_t) glove_frame_read_u16_le(p_data);
}

/*
 * 内部函数作用：
 *   按小端格式读取 32 位无符号整数。
 * 参数说明：
 *   p_data: 指向 4 字节源数据。
 * 返回值：
 *   还原后的 uint32_t 数值。
 */
static uint32_t glove_frame_read_u32_le (uint8_t const * p_data)
{
    return ((uint32_t) p_data[0]) |
           ((uint32_t) p_data[1] << 8) |
           ((uint32_t) p_data[2] << 16) |
           ((uint32_t) p_data[3] << 24);
}

/*
 * 内部函数作用：
 *   当 31 字节已收满但校验失败时，在当前缓存中尝试寻找下一组潜在帧头。
 * 调用关系：
 *   仅由 GloveFrame_Receive 在重同步时调用。
 * 参数说明：
 *   p_packet: 当前 31 字节缓存。
 * 返回值：
 *   返回保留下来的有效缓存长度；
 *   返回 0 表示当前缓存内没有可继续利用的帧头。
 */
static uint32_t glove_frame_resync_after_invalid (uint8_t * p_packet)
{
    uint32_t start_index;

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

/*
 * 内部函数作用：
 *   计算一段数据的 CRC16-CCITT-FALSE。
 * 调用关系：
 *   仅由本模块内部解包函数使用。
 * 参数说明：
 *   p_data: 参与校验的数据首地址。
 *   length: 参与校验的字节数。
 * 返回值：
 *   计算得到的 CRC16 值；
 *   如果数据指针为空，返回 0。
 */
static uint16_t glove_frame_crc16 (uint8_t const * p_data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;

    if (NULL == p_data)
    {
        return 0U;
    }

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

/*
 * 内部函数作用：
 *   对一帧完整的 31 字节缓存做协议校验和字段解包。
 * 调用关系：
 *   仅由 GloveFrame_Receive 在收满一帧后调用。
 * 参数说明：
 *   p_packet: 完整帧缓存。
 *   p_frame: 输出结构体地址。
 * 返回值：
 *   FSP_SUCCESS 表示解包成功；
 *   FSP_ERR_INVALID_DATA 表示帧头、版本或 CRC 不合法；
 *   FSP_ERR_ASSERTION 表示输入参数为空。
 */
static fsp_err_t glove_frame_parse_packet (uint8_t const * p_packet, glove_frame_t * p_frame)
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
    calculated_crc = glove_frame_crc16(p_packet, GLOVE_FRAME_PAYLOAD_WITHOUT_CRC);

    if (received_crc != calculated_crc)
    {
        return FSP_ERR_INVALID_DATA;
    }

    /*
     * 只有协议头、版本和 CRC 全都通过后，才把结果写到输出结构体，
     * 这样上层永远拿到的是一帧完整、干净的数据。
     */
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

/*
 * 函数作用：
 *   从串口流里同步并解出一帧合法协议数据。
 * 参数说明：
 *   rx_port: 数据来源串口。
 *   p_frame: 成功时写入解析结果的结构体地址。
 * 返回值：
 *   FSP_SUCCESS 表示已拿到一帧完整合法数据；
 *   FSP_ERR_ASSERTION 表示输出指针为空；
 *   其他错误码表示底层接收失败。
 * 调用方式：
 *   应用层主循环反复调用本函数，每成功一次，就得到一帧新的手套特征数据。
 */
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
     * 运行流程：
     * 1. 逐字节从串口读取数据；
     * 2. 先在流中寻找帧头 AA 55；
     * 3. 找到帧头后继续攒满整帧 31 字节；
     * 4. 收满后做版本和 CRC 校验；
     * 5. 如果失败，则在当前缓存内继续重同步；
     * 6. 直到得到一帧合法数据才返回。
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
                packet[0]     = rx_byte;
                cached_length = 1U;
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

        err = glove_frame_parse_packet(packet, p_frame);
        if (FSP_SUCCESS == err)
        {
            return FSP_SUCCESS;
        }

        cached_length = glove_frame_resync_after_invalid(packet);
    }
}
