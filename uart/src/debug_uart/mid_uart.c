#include "mid_uart.h"

#include <stddef.h>
#include <string.h>

/* 文本格式化浮点数时使用的本地缓冲区长度。 */
#define MID_UART_FLOAT_BUFFER_SIZE        (32U)
/* 小数位最大保留 6 位，防止输出过长。 */
#define MID_UART_MAX_FLOAT_TEXT_PRECISION (6U)

/*
 * 内部函数作用：
 *   计算 10 的若干次方，用于浮点文本格式化时确定小数缩放因子。
 * 调用关系：
 *   仅供 mid_uart_format_float 使用。
 * 参数说明：
 *   precision: 小数位数。
 * 返回值：
 *   10 的 precision 次方。
 */
static uint32_t mid_uart_pow10 (uint8_t precision)
{
    uint32_t result = 1U;

    for (uint8_t i = 0; i < precision; i++)
    {
        result *= 10U;
    }

    return result;
}

/*
 * 内部函数作用：
 *   把一个无符号整数转成十进制字符串。
 * 调用关系：
 *   仅供浮点文本格式化函数使用。
 * 参数说明：
 *   value: 待格式化的无符号整数。
 *   p_buffer: 输出缓冲区，必须足够大。
 * 返回值：
 *   实际写入的字符数，不包含字符串结尾。
 */
static uint32_t mid_uart_write_unsigned (uint32_t value, char * p_buffer)
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

/*
 * 内部函数作用：
 *   把 float 手工格式化成文本，避免裸机工程依赖 printf 的浮点格式化能力。
 * 调用关系：
 *   仅供 MID_Uart_SendFloatText 使用。
 * 参数说明：
 *   value: 待格式化的浮点数。
 *   precision: 期望小数位数。
 *   p_buffer: 输出文本缓冲区。
 *   buffer_size: 缓冲区总长度。
 * 返回值：
 *   返回生成的文本长度，不包含结尾 '\0'；
 *   返回 0 表示格式化失败。
 */
static uint32_t mid_uart_format_float (float value, uint8_t precision, char * p_buffer, uint32_t buffer_size)
{
    uint32_t scale;
    uint32_t integer_part;
    uint32_t fraction_part;
    uint32_t index = 0U;

    if ((NULL == p_buffer) || (buffer_size < 4U))
    {
        return 0U;
    }

    if (precision > MID_UART_MAX_FLOAT_TEXT_PRECISION)
    {
        precision = MID_UART_MAX_FLOAT_TEXT_PRECISION;
    }

    if (value < 0.0f)
    {
        p_buffer[index++] = '-';
        value             = -value;
    }

    /*
     * 实现思路：
     * 1. 先拆出整数部分；
     * 2. 再按 precision 把小数部分放大成整数；
     * 3. 最后手工拼接成十进制文本。
     */
    scale         = mid_uart_pow10(precision);
    integer_part  = (uint32_t) value;
    fraction_part = (uint32_t) (((value - (float) integer_part) * (float) scale) + 0.5f);

    if (fraction_part >= scale)
    {
        integer_part += 1U;
        fraction_part -= scale;
    }

    index += mid_uart_write_unsigned(integer_part, &p_buffer[index]);

    if (precision > 0U)
    {
        p_buffer[index++] = '.';

        for (uint32_t divisor = scale / 10U; divisor > 0U; divisor /= 10U)
        {
            p_buffer[index++] = (char) ('0' + ((fraction_part / divisor) % 10U));
        }
    }

    if (index >= buffer_size)
    {
        return 0U;
    }

    p_buffer[index] = '\0';

    return index;
}

/*
 * 函数作用：
 *   初始化 UART 中间层。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示初始化完成；
 *   其他错误码透传自板级驱动。
 * 调用方式：
 *   应用层启动时调用一次。
 */
fsp_err_t MID_Uart_Init (void)
{
    return BSP_DebugUart_InitAll();
}

/*
 * 函数作用：
 *   发送原始字节流。
 * 参数说明：
 *   port: 目标串口号。
 *   p_data: 待发送数据首地址。
 *   length: 数据长度。
 * 返回值：
 *   FSP_SUCCESS 表示发送完成；
 *   其他错误码透传自板级阻塞发送接口。
 * 调用方式：
 *   中间层统一使用阻塞式语义，业务层调用后即可认为发送已经结束。
 */
fsp_err_t MID_Uart_SendBytes (mid_uart_port_t port, uint8_t const * p_data, uint32_t length)
{
    return BSP_DebugUart_WriteBlocking(port, p_data, length);
}

/*
 * 函数作用：
 *   接收固定长度字节流。
 * 参数说明：
 *   port: 来源串口号。
 *   p_data: 接收缓冲区。
 *   length: 期望接收字节数。
 * 返回值：
 *   FSP_SUCCESS 表示收满指定长度；
 *   其他错误码透传自板级阻塞接收接口。
 * 调用方式：
 *   协议层收帧、逐字节同步等逻辑都依赖本函数。
 */
fsp_err_t MID_Uart_ReceiveBytes (mid_uart_port_t port, uint8_t * p_data, uint32_t length)
{
    return BSP_DebugUart_ReadBlocking(port, p_data, length);
}

/*
 * 函数作用：
 *   发送普通字符串文本。
 * 参数说明：
 *   port: 目标串口号。
 *   p_string: 字符串首地址。
 * 返回值：
 *   FSP_SUCCESS 表示发送完成；
 *   FSP_ERR_ASSERTION 表示字符串为空；
 *   其他错误码透传自底层发送。
 * 调用方式：
 *   适合发送 CSV 分隔符、换行和其他纯文本内容。
 */
fsp_err_t MID_Uart_SendString (mid_uart_port_t port, char const * p_string)
{
    if (NULL == p_string)
    {
        return FSP_ERR_ASSERTION;
    }

    return MID_Uart_SendBytes(port, (uint8_t const *) p_string, (uint32_t) strlen(p_string));
}

/*
 * 函数作用：
 *   发送命令字节。
 * 参数说明：
 *   port: 目标串口号。
 *   p_data: 命令数据首地址。
 *   length: 命令字节数。
 * 返回值：
 *   FSP_SUCCESS 表示发送完成；
 *   其他错误码透传自 MID_Uart_SendBytes。
 * 调用方式：
 *   当前识别模式下，uart7 发送控制命令时通过本函数完成。
 */
fsp_err_t MID_Uart_SendHex (mid_uart_port_t port, uint8_t const * p_data, uint32_t length)
{
    return MID_Uart_SendBytes(port, p_data, length);
}

/*
 * 函数作用：
 *   发送一个格式化后的浮点文本。
 * 参数说明：
 *   port: 目标串口号。
 *   value: 浮点值。
 *   precision: 小数位数。
 *   append_newline: 是否在末尾追加 "\r\n"。
 * 返回值：
 *   FSP_SUCCESS 表示格式化和发送都成功；
 *   FSP_ERR_INVALID_ARGUMENT 表示格式化失败或缓冲区不足；
 *   其他错误码透传自底层发送。
 * 调用方式：
 *   CSV 导出时每个特征值都通过本函数转成文本。
 */
fsp_err_t MID_Uart_SendFloatText (mid_uart_port_t port, float value, uint8_t precision, bool append_newline)
{
    char     buffer[MID_UART_FLOAT_BUFFER_SIZE];
    uint32_t length = mid_uart_format_float(value, precision, buffer, sizeof(buffer));

    if (0U == length)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (append_newline)
    {
        if ((length + 2U) >= sizeof(buffer))
        {
            return FSP_ERR_INVALID_ARGUMENT;
        }

        buffer[length++] = '\r';
        buffer[length++] = '\n';
        buffer[length]   = '\0';
    }

    return MID_Uart_SendBytes(port, (uint8_t const *) buffer, length);
}
