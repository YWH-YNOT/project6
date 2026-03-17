#ifndef DEBUG_UART_MID_UART_H_
#define DEBUG_UART_MID_UART_H_

#include "bsp_debug_uart.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * 中间层 UART 服务
 * 这一层不直接操作硬件寄存器，也不直接调用 g_uart2/g_uart7。
 * 它只依赖板级层提供的通用收发能力，并继续向应用层暴露更容易使用的接口：
 * 1. 字节流收发
 * 2. 字符串收发
 * 3. 浮点数文本/二进制收发
 * 4. 16 进制命令收发
 */
typedef bsp_debug_uart_port_t mid_uart_port_t;

#define MID_UART_PORT_2 BSP_DEBUG_UART_PORT_2
#define MID_UART_PORT_7 BSP_DEBUG_UART_PORT_7

fsp_err_t MID_Uart_Init(void);

/* 发送原始字节流，适合任意二进制协议。 */
fsp_err_t MID_Uart_SendBytes(mid_uart_port_t port, uint8_t const * p_data, uint32_t length);
/* 接收原始字节流，适合定长协议或帧缓存。 */
fsp_err_t MID_Uart_ReceiveBytes(mid_uart_port_t port, uint8_t * p_data, uint32_t length);
/* 发送 C 字符串，不包含结尾 '\0'。 */
fsp_err_t MID_Uart_SendString(mid_uart_port_t port, char const * p_string);

/* 发送十六进制原始数据，本质上就是发送二进制字节。 */
fsp_err_t MID_Uart_SendHex(mid_uart_port_t port, uint8_t const * p_data, uint32_t length);
/* 接收十六进制原始数据，本质上就是接收二进制字节。 */
fsp_err_t MID_Uart_ReceiveHex(mid_uart_port_t port, uint8_t * p_data, uint32_t length);
/* 将 "AA 55 01 02" 这样的字符串解析成字节后再发送。 */
fsp_err_t MID_Uart_SendHexString(mid_uart_port_t port, char const * p_hex_string);

/* 以文本形式发送浮点数，例如 "36.58\r\n"。 */
fsp_err_t MID_Uart_SendFloatText(mid_uart_port_t port, float value, uint8_t precision, bool append_newline);
/* 从串口接收一个文本浮点数，并解析成 float。 */
fsp_err_t MID_Uart_ReceiveFloatText(mid_uart_port_t port, float * p_value);

/* 以 IEEE754 二进制形式连续发送多个 float。 */
fsp_err_t MID_Uart_SendFloatBinary(mid_uart_port_t port, float const * p_values, uint32_t count);
/* 以 IEEE754 二进制形式连续接收多个 float。 */
fsp_err_t MID_Uart_ReceiveFloatBinary(mid_uart_port_t port, float * p_values, uint32_t count);

#endif /* DEBUG_UART_MID_UART_H_ */
