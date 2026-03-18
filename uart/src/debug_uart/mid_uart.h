#ifndef DEBUG_UART_MID_UART_H_
#define DEBUG_UART_MID_UART_H_

#include "bsp_debug_uart.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * 模块说明：
 * 1. 本模块属于 UART 中间层，不直接关心 g_uart2/g_uart7 这些具体硬件实例。
 * 2. 它把板级驱动重新整理成更符合业务习惯的接口：字节流、字符串、浮点文本、命令字节。
 * 3. 当前工程选择统一使用阻塞式收发语义，应用层拿到函数后就能直接用。
 */
typedef bsp_debug_uart_port_t mid_uart_port_t;

#define MID_UART_PORT_2 BSP_DEBUG_UART_PORT_2
#define MID_UART_PORT_7 BSP_DEBUG_UART_PORT_7

/*
 * 函数作用：
 *   初始化中间层用到的所有串口。
 * 调用时机：
 *   系统启动阶段由应用层调用一次。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示所有串口准备完成；
 *   其他错误码透传自板级串口驱动。
 * 调用方式：
 *   初始化成功后，才能继续调用本模块其他收发接口。
 */
fsp_err_t MID_Uart_Init(void);

/*
 * 函数作用：
 *   发送原始字节流。
 * 调用时机：
 *   发送固定长度协议帧、命令字节或任意二进制数据时调用。
 * 参数说明：
 *   port: 目标逻辑串口号。
 *   p_data: 待发送数据首地址。
 *   length: 待发送字节数。
 * 返回值：
 *   FSP_SUCCESS 表示发送完成；
 *   其他错误码透传自板级阻塞发送接口。
 * 调用方式：
 *   这是本模块最基础的发送接口，其他发送接口本质上都会落到这里。
 */
fsp_err_t MID_Uart_SendBytes(mid_uart_port_t port, uint8_t const * p_data, uint32_t length);

/*
 * 函数作用：
 *   接收固定长度原始字节流。
 * 调用时机：
 *   协议层需要同步收取固定长度数据时调用。
 * 参数说明：
 *   port: 来源串口号。
 *   p_data: 接收缓冲区首地址。
 *   length: 需要接收的字节数。
 * 返回值：
 *   FSP_SUCCESS 表示已经收满指定字节数；
 *   其他错误码透传自板级阻塞接收接口。
 * 调用方式：
 *   协议同步、按长度收帧时都通过本函数完成。
 */
fsp_err_t MID_Uart_ReceiveBytes(mid_uart_port_t port, uint8_t * p_data, uint32_t length);

/*
 * 函数作用：
 *   发送 C 字符串，不包含末尾的 '\0'。
 * 调用时机：
 *   发送文本提示、CSV 分隔符、换行符等场景调用。
 * 参数说明：
 *   port: 目标串口号。
 *   p_string: 待发送字符串首地址，不能为空。
 * 返回值：
 *   FSP_SUCCESS 表示字符串已经发送完成；
 *   FSP_ERR_ASSERTION 表示字符串指针为空；
 *   其他错误码透传自字节流发送接口。
 * 调用方式：
 *   适合文本协议和调试输出，不适合发送包含 '\0' 的二进制数据。
 */
fsp_err_t MID_Uart_SendString(mid_uart_port_t port, char const * p_string);

/*
 * 函数作用：
 *   发送命令字节流。
 * 调用时机：
 *   当前主要用于 uart7 向外发送 1 字节控制命令。
 * 参数说明：
 *   port: 目标串口号。
 *   p_data: 命令数据首地址。
 *   length: 命令字节数。
 * 返回值：
 *   FSP_SUCCESS 表示命令发送完成；
 *   其他错误码透传自 MID_Uart_SendBytes。
 * 调用方式：
 *   这是对“命令发送”场景的语义化封装，本质上仍然是二进制发送。
 */
fsp_err_t MID_Uart_SendHex(mid_uart_port_t port, uint8_t const * p_data, uint32_t length);

/*
 * 函数作用：
 *   以文本形式发送一个浮点数，可按需追加换行。
 * 调用时机：
 *   当前主要用于采集模式下导出 CSV 特征文本。
 * 参数说明：
 *   port: 目标串口号。
 *   value: 要发送的浮点值。
 *   precision: 小数位位数，上限由模块内部限制。
 *   append_newline: 为 true 时在数值后追加 "\r\n"。
 * 返回值：
 *   FSP_SUCCESS 表示格式化和发送都成功；
 *   FSP_ERR_INVALID_ARGUMENT 表示格式化失败或缓冲区长度不够；
 *   其他错误码透传自底层发送。
 * 调用方式：
 *   发送 CSV 某一列时传 false，发送一整行末尾时再通过上层补分隔符或换行。
 */
fsp_err_t MID_Uart_SendFloatText(mid_uart_port_t port, float value, uint8_t precision, bool append_newline);

#endif /* DEBUG_UART_MID_UART_H_ */
