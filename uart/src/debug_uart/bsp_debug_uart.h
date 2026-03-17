#ifndef DEBUG_UART_BSP_DEBUG_UART_H_
#define DEBUG_UART_BSP_DEBUG_UART_H_

#include "hal_data.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * 板级 UART 驱动层
 * 这一层只关心具体硬件资源：uart2、uart7、FSP 的 open/read/write/callback。
 * 上层业务不应该直接接触 g_uart2、g_uart7，而是通过这里提供的统一接口访问串口。
 */
typedef enum e_bsp_debug_uart_port
{
    /* 对应 FSP 里配置的 SCI UART2。 */
    BSP_DEBUG_UART_PORT_2 = 0,
    /* 对应 FSP 里配置的 SCI UART7。 */
    BSP_DEBUG_UART_PORT_7,
    /* 端口数量上限，用于内部数组管理。 */
    BSP_DEBUG_UART_PORT_MAX
} bsp_debug_uart_port_t;

/* 初始化指定串口。若已经打开，则直接返回成功。 */
fsp_err_t BSP_DebugUart_Init(bsp_debug_uart_port_t port);
/* 一次性初始化 uart2 和 uart7，便于中间层统一启动。 */
fsp_err_t BSP_DebugUart_InitAll(void);
/* 关闭指定串口，给后续扩展节能或重配波特率预留接口。 */
fsp_err_t BSP_DebugUart_Deinit(bsp_debug_uart_port_t port);
/* 查询串口是否已经完成 open。 */
bool BSP_DebugUart_IsOpen(bsp_debug_uart_port_t port);

/* 启动一次非阻塞发送，真正完成发送需要等待回调置位。 */
fsp_err_t BSP_DebugUart_Write(bsp_debug_uart_port_t port, uint8_t const * p_data, uint32_t length);
/* 启动一次非阻塞接收，真正完成接收需要等待回调置位。 */
fsp_err_t BSP_DebugUart_Read(bsp_debug_uart_port_t port, uint8_t * p_data, uint32_t length);

/* 阻塞式发送接口，适合上层直接发送一帧数据。 */
fsp_err_t BSP_DebugUart_WriteBlocking(bsp_debug_uart_port_t port, uint8_t const * p_data, uint32_t length);
/* 阻塞式接收接口，适合上层按固定长度取一帧数据。 */
fsp_err_t BSP_DebugUart_ReadBlocking(bsp_debug_uart_port_t port, uint8_t * p_data, uint32_t length);
/* 阻塞接收 1 个字节，便于上层按字节解析文本或协议。 */
fsp_err_t BSP_DebugUart_ReadByteBlocking(bsp_debug_uart_port_t port, uint8_t * p_byte);

/* FSP 自动生成代码会在 hal_data.h 中声明这两个回调，这里由板级层统一接管。 */
void uart2_callback(uart_callback_args_t * p_args);
void uart7_callback(uart_callback_args_t * p_args);

#endif /* DEBUG_UART_BSP_DEBUG_UART_H_ */
