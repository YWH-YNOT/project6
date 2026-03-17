#include "mid_uart.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* 文本浮点格式化缓存长度，足够覆盖常见调试场景。 */
#define MID_UART_FLOAT_BUFFER_SIZE        (32U)
/* 接收文本浮点时的最大字符长度，超出则视为异常输入。 */
#define MID_UART_FLOAT_TEXT_MAX_LENGTH    (31U)
/* 16 进制字符串命令允许转换后的最大字节数。 */
#define MID_UART_MAX_HEX_COMMAND_BYTES    (64U)
/* 小数位最大保留 6 位，兼顾表达能力与实现复杂度。 */
#define MID_UART_MAX_FLOAT_TEXT_PRECISION (6U)

static bool mid_uart_is_space (char ch)
{
    /* 统一把空格、回车、换行、制表符和逗号都当成分隔符。 */
    return (' ' == ch) || ('\r' == ch) || ('\n' == ch) || ('\t' == ch) || (',' == ch);
}

static bool mid_uart_is_float_char (char ch)
{
    /* 允许解析常见的浮点文本格式，例如 -12.34、1.2e-3。 */
    return ((ch >= '0') && (ch <= '9')) ||
           ('+' == ch) ||
           ('-' == ch) ||
           ('.' == ch) ||
           ('e' == ch) ||
           ('E' == ch);
}

static bool mid_uart_is_hex_char (char ch)
{
    /* 判断字符是否为合法十六进制字符。 */
    return ((ch >= '0') && (ch <= '9')) ||
           ((ch >= 'a') && (ch <= 'f')) ||
           ((ch >= 'A') && (ch <= 'F'));
}

static uint8_t mid_uart_hex_to_nibble (char ch)
{
    /* 将单个十六进制字符转成 0~15 的数值。 */
    if ((ch >= '0') && (ch <= '9'))
    {
        return (uint8_t) (ch - '0');
    }

    if ((ch >= 'a') && (ch <= 'f'))
    {
        return (uint8_t) (ch - 'a' + 10);
    }

    return (uint8_t) (ch - 'A' + 10);
}

static uint32_t mid_uart_pow10 (uint8_t precision)
{
    uint32_t result = 1U;

    /* 计算 10 的 precision 次方，用于浮点文本格式化。 */
    for (uint8_t i = 0; i < precision; i++)
    {
        result *= 10U;
    }

    return result;
}

static uint32_t mid_uart_write_unsigned (uint32_t value, char * p_buffer)
{
    char     temp[10];
    uint32_t count = 0U;
    uint32_t index = 0U;

    /* 单独处理 0，避免后面的循环不进入。 */
    if (0U == value)
    {
        p_buffer[0] = '0';
        return 1U;
    }

    /* 先逆序拆分十进制字符，再翻转写回目标缓冲区。 */
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

static uint32_t mid_uart_format_float (float value, uint8_t precision, char * p_buffer, uint32_t buffer_size)
{
    uint32_t scale;
    uint32_t integer_part;
    uint32_t fraction_part;
    uint32_t index = 0U;

    /* 缓冲区太小就直接判失败，避免写越界。 */
    if ((NULL == p_buffer) || (buffer_size < 4U))
    {
        return 0U;
    }

    /* 精度做上限保护，避免文本过长。 */
    if (precision > MID_UART_MAX_FLOAT_TEXT_PRECISION)
    {
        precision = MID_UART_MAX_FLOAT_TEXT_PRECISION;
    }

    /* 负数先输出符号位，再转成正数处理。 */
    if (value < 0.0f)
    {
        p_buffer[index++] = '-';
        value             = -value;
    }

    /* 将浮点数拆成整数部分和小数部分，再手工格式化，避免依赖 printf 浮点格式。 */
    scale         = mid_uart_pow10(precision);
    integer_part  = (uint32_t) value;
    fraction_part = (uint32_t) (((value - (float) integer_part) * (float) scale) + 0.5f);

    /* 四舍五入可能导致小数部分进位，需要补偿到整数部分。 */
    if (fraction_part >= scale)
    {
        integer_part += 1U;
        fraction_part -= scale;
    }

    /* 先输出整数部分。 */
    index += mid_uart_write_unsigned(integer_part, &p_buffer[index]);

    if (precision > 0U)
    {
        /* 逐位输出小数部分，保证位数固定。 */
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

fsp_err_t MID_Uart_Init (void)
{
    /* 中间层初始化只做一件事：统一初始化底层所有串口。 */
    return BSP_DebugUart_InitAll();
}

fsp_err_t MID_Uart_SendBytes (mid_uart_port_t port, uint8_t const * p_data, uint32_t length)
{
    /* 中间层默认提供阻塞式发送，应用层调用更直接。 */
    return BSP_DebugUart_WriteBlocking(port, p_data, length);
}

fsp_err_t MID_Uart_ReceiveBytes (mid_uart_port_t port, uint8_t * p_data, uint32_t length)
{
    /* 中间层默认提供阻塞式接收，适合当前裸机项目。 */
    return BSP_DebugUart_ReadBlocking(port, p_data, length);
}

fsp_err_t MID_Uart_SendString (mid_uart_port_t port, char const * p_string)
{
    /* C 字符串只发送有效字符，不发送 '\0'。 */
    if (NULL == p_string)
    {
        return FSP_ERR_ASSERTION;
    }

    return MID_Uart_SendBytes(port, (uint8_t const *) p_string, (uint32_t) strlen(p_string));
}

fsp_err_t MID_Uart_SendHex (mid_uart_port_t port, uint8_t const * p_data, uint32_t length)
{
    /* 对于串口来说，十六进制命令最终就是一串字节。 */
    return MID_Uart_SendBytes(port, p_data, length);
}

fsp_err_t MID_Uart_ReceiveHex (mid_uart_port_t port, uint8_t * p_data, uint32_t length)
{
    /* 对于串口来说，接收十六进制命令最终也是接收字节流。 */
    return MID_Uart_ReceiveBytes(port, p_data, length);
}

fsp_err_t MID_Uart_SendHexString (mid_uart_port_t port, char const * p_hex_string)
{
    uint8_t  buffer[MID_UART_MAX_HEX_COMMAND_BYTES];
    uint32_t count = 0U;
    char     high = 0;
    bool     has_high_nibble = false;

    /* 逐个字符扫描，把文本十六进制命令转换成真实字节。 */
    if (NULL == p_hex_string)
    {
        return FSP_ERR_ASSERTION;
    }

    while ('\0' != *p_hex_string)
    {
        char ch = *p_hex_string++;

        /* 允许字符串里带空格和换行，便于人工书写调试命令。 */
        if (mid_uart_is_space(ch))
        {
            continue;
        }

        /* 一旦出现非法十六进制字符，直接返回参数错误。 */
        if (!mid_uart_is_hex_char(ch))
        {
            return FSP_ERR_INVALID_ARGUMENT;
        }

        /* 先收高 4 位，等下一个字符来了再拼成完整字节。 */
        if (!has_high_nibble)
        {
            high            = ch;
            has_high_nibble = true;
            continue;
        }

        /* 做长度保护，防止命令过长写爆栈上缓冲区。 */
        if (count >= MID_UART_MAX_HEX_COMMAND_BYTES)
        {
            return FSP_ERR_INVALID_ARGUMENT;
        }

        buffer[count++] = (uint8_t) ((mid_uart_hex_to_nibble(high) << 4) | mid_uart_hex_to_nibble(ch));
        has_high_nibble = false;
    }

    /* 单数个十六进制字符说明输入不完整，例如 "AA 5"。 */
    if (has_high_nibble)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    return MID_Uart_SendHex(port, buffer, count);
}

fsp_err_t MID_Uart_SendFloatText (mid_uart_port_t port, float value, uint8_t precision, bool append_newline)
{
    char     buffer[MID_UART_FLOAT_BUFFER_SIZE];
    uint32_t length = mid_uart_format_float(value, precision, buffer, sizeof(buffer));

    /* 先把 float 格式化成文本，再统一走字节发送接口。 */
    if (0U == length)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    /* 可选自动追加回车换行，便于和上位机串口助手联调。 */
    if (append_newline)
    {
        buffer[length++] = '\r';
        buffer[length++] = '\n';
        buffer[length]   = '\0';
    }

    return MID_Uart_SendBytes(port, (uint8_t const *) buffer, length);
}

fsp_err_t MID_Uart_ReceiveFloatText (mid_uart_port_t port, float * p_value)
{
    char      buffer[MID_UART_FLOAT_TEXT_MAX_LENGTH + 1U];
    uint32_t  index = 0U;
    uint8_t   rx_byte;
    fsp_err_t err;

    /* 先跳过输入流前面的空白字符，避免解析受到回车换行影响。 */
    if (NULL == p_value)
    {
        return FSP_ERR_ASSERTION;
    }

    do
    {
        err = BSP_DebugUart_ReadByteBlocking(port, &rx_byte);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    } while (mid_uart_is_space((char) rx_byte));

    /* 连续读取合法浮点字符，直到遇到分隔符或长度上限。 */
    while (index < MID_UART_FLOAT_TEXT_MAX_LENGTH)
    {
        if (!mid_uart_is_float_char((char) rx_byte))
        {
            break;
        }

        buffer[index++] = (char) rx_byte;

        err = BSP_DebugUart_ReadByteBlocking(port, &rx_byte);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        if (mid_uart_is_space((char) rx_byte))
        {
            break;
        }
    }

    buffer[index] = '\0';

    /* 至少要收到 1 个有效字符，否则说明输入不合法。 */
    if (0U == index)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    /* 使用标准库将文本转换成 float，降低手写解析复杂度。 */
    *p_value = strtof(buffer, NULL);

    return FSP_SUCCESS;
}

fsp_err_t MID_Uart_SendFloatBinary (mid_uart_port_t port, float const * p_values, uint32_t count)
{
    /* 二进制浮点发送适合 MCU 之间的高效传输。 */
    if ((NULL == p_values) || (0U == count))
    {
        return FSP_ERR_ASSERTION;
    }

    return MID_Uart_SendBytes(port, (uint8_t const *) p_values, count * (uint32_t) sizeof(float));
}

fsp_err_t MID_Uart_ReceiveFloatBinary (mid_uart_port_t port, float * p_values, uint32_t count)
{
    /* 二进制浮点接收与发送保持对称，方便协议设计。 */
    if ((NULL == p_values) || (0U == count))
    {
        return FSP_ERR_ASSERTION;
    }

    return MID_Uart_ReceiveBytes(port, (uint8_t *) p_values, count * (uint32_t) sizeof(float));
}
