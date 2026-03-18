#ifndef DEBUG_UART_BSP_DEBUG_UART_H_
#define DEBUG_UART_BSP_DEBUG_UART_H_

#include "hal_data.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * 模块说明：
 * 1. 本模块属于板级驱动层，只封装 FSP 已经生成好的 uart2 和 uart7。
 * 2. 上层不能直接操作 g_uart2、g_uart7，而是通过这里定义的统一接口访问串口。
 * 3. 本模块屏蔽了 FSP 回调、完成标志和 open/close 细节，让上层只关心“哪个串口”“发什么”“收什么”。
 */
typedef enum e_bsp_debug_uart_port
{
    /* 逻辑串口 2，对应 FSP 配置里的 SCI UART2。 */
    BSP_DEBUG_UART_PORT_2 = 0,
    /* 逻辑串口 7，对应 FSP 配置里的 SCI UART7。 */
    BSP_DEBUG_UART_PORT_7,
    /* 端口数量上限，仅供模块内部数组管理使用。 */
    BSP_DEBUG_UART_PORT_MAX
} bsp_debug_uart_port_t;

/*
 * 函数作用：
 *   初始化指定逻辑串口对应的硬件实例。
 * 调用时机：
 *   系统启动阶段由中间层调用一次；如果后续需要重新打开串口，也可以再次调用。
 * 参数说明：
 *   port: 要初始化的逻辑串口号，取值见 bsp_debug_uart_port_t。
 * 返回值：
 *   FSP_SUCCESS 表示初始化成功，或者该串口本来就已经处于打开状态；
 *   其他错误码表示 FSP open 失败。
 * 调用方式：
 *   先调用本函数，再调用收发接口。
 */
fsp_err_t BSP_DebugUart_Init(bsp_debug_uart_port_t port);

/*
 * 函数作用：
 *   一次性初始化当前工程会用到的所有调试串口。
 * 调用时机：
 *   通常由中间层在系统启动阶段调用一次。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示 uart2 和 uart7 都已准备完成；
 *   其他错误码表示其中某个串口初始化失败。
 * 调用方式：
 *   对于当前工程，优先调用本函数，而不是分别调用单口初始化。
 */
fsp_err_t BSP_DebugUart_InitAll(void);

/*
 * 函数作用：
 *   关闭指定串口，为后续重开、重配或扩展功能预留接口。
 * 调用时机：
 *   当前工程默认不会主动关闭串口；只有后续需要释放资源时才调用。
 * 参数说明：
 *   port: 要关闭的逻辑串口号。
 * 返回值：
 *   FSP_SUCCESS 表示关闭成功，或者串口本来就没有打开；
 *   其他错误码表示 FSP close 失败。
 * 调用方式：
 *   调用后若还要继续收发，需要重新执行 BSP_DebugUart_Init。
 */
fsp_err_t BSP_DebugUart_Deinit(bsp_debug_uart_port_t port);

/*
 * 函数作用：
 *   查询指定串口当前是否已经完成 open。
 * 调用时机：
 *   上层需要判断串口是否可用时调用。
 * 参数说明：
 *   port: 要查询的逻辑串口号。
 * 返回值：
 *   true 表示已打开；
 *   false 表示未打开或端口号非法。
 * 调用方式：
 *   本函数只做状态查询，不会触发任何硬件动作。
 */
bool BSP_DebugUart_IsOpen(bsp_debug_uart_port_t port);

/*
 * 函数作用：
 *   启动一次非阻塞发送。
 * 调用时机：
 *   当上层希望先发起发送，再由别的逻辑等待发送完成时调用。
 * 参数说明：
 *   port: 目标逻辑串口号。
 *   p_data: 待发送数据首地址，不能为空。
 *   length: 待发送字节数，必须大于 0。
 * 返回值：
 *   FSP_SUCCESS 表示发送任务已经提交给底层驱动；
 *   FSP_ERR_NOT_OPEN 表示串口尚未初始化；
 *   FSP_ERR_ASSERTION 表示参数非法。
 * 调用方式：
 *   调用后如果需要等待真正发送完成，应继续配合阻塞接口或自定义等待逻辑使用。
 */
fsp_err_t BSP_DebugUart_Write(bsp_debug_uart_port_t port, uint8_t const * p_data, uint32_t length);

/*
 * 函数作用：
 *   启动一次非阻塞接收。
 * 调用时机：
 *   当上层希望先发起接收，再由别的逻辑等待接收完成时调用。
 * 参数说明：
 *   port: 数据来源串口号。
 *   p_data: 接收缓冲区首地址，不能为空。
 *   length: 期望接收的字节数，必须大于 0。
 * 返回值：
 *   FSP_SUCCESS 表示接收任务已经提交给底层驱动；
 *   FSP_ERR_NOT_OPEN 表示串口尚未初始化；
 *   FSP_ERR_ASSERTION 表示参数非法。
 * 调用方式：
 *   调用后若需要同步等待数据收满，应优先使用 BSP_DebugUart_ReadBlocking。
 */
fsp_err_t BSP_DebugUart_Read(bsp_debug_uart_port_t port, uint8_t * p_data, uint32_t length);

/*
 * 函数作用：
 *   发送一段数据，并一直等待到底层回调确认发送完成。
 * 调用时机：
 *   当前裸机工程大多数场景都直接使用这个接口。
 * 参数说明：
 *   port: 目标串口号。
 *   p_data: 待发送数据首地址。
 *   length: 待发送字节数。
 * 返回值：
 *   FSP_SUCCESS 表示整段数据已经发送完成；
 *   其他错误码表示启动发送失败或串口未打开。
 * 调用方式：
 *   识别命令发送、CSV 文本发送都通过这个阻塞接口完成。
 */
fsp_err_t BSP_DebugUart_WriteBlocking(bsp_debug_uart_port_t port, uint8_t const * p_data, uint32_t length);

/*
 * 函数作用：
 *   接收一段固定长度的数据，并一直等待到底层回调确认接收完成。
 * 调用时机：
 *   当前裸机工程需要同步拿到完整协议帧时调用。
 * 参数说明：
 *   port: 数据来源串口号。
 *   p_data: 接收缓冲区首地址。
 *   length: 需要接收的字节数。
 * 返回值：
 *   FSP_SUCCESS 表示指定长度的数据已经全部接收完成；
 *   其他错误码表示启动接收失败或串口未打开。
 * 调用方式：
 *   协议层按固定长度取帧、按字节同步帧头时都会间接使用本函数。
 */
fsp_err_t BSP_DebugUart_ReadBlocking(bsp_debug_uart_port_t port, uint8_t * p_data, uint32_t length);

/*
 * 函数作用：
 *   从指定串口阻塞接收 1 个字节。
 * 调用时机：
 *   上层需要逐字节做协议同步或文本解析时调用。
 * 参数说明：
 *   port: 数据来源串口号。
 *   p_byte: 接收单字节的目标地址，不能为空。
 * 返回值：
 *   FSP_SUCCESS 表示成功收到了 1 个字节；
 *   其他错误码表示串口未打开或底层接收失败。
 * 调用方式：
 *   本函数是对 BSP_DebugUart_ReadBlocking 的单字节封装，便于协议层逐字节扫描。
 */
fsp_err_t BSP_DebugUart_ReadByteBlocking(bsp_debug_uart_port_t port, uint8_t * p_byte);

/*
 * 函数作用：
 *   FSP 为 uart2 生成的回调入口，由本模块统一接管。
 * 调用时机：
 *   不能手动调用，只会在 uart2 发生发送完成或接收完成事件时由底层自动触发。
 * 参数说明：
 *   p_args: FSP 传入的 UART 事件参数。
 * 返回值：
 *   无。
 * 调用方式：
 *   只需要保证函数名与 FSP 配置一致即可，上层不直接调用。
 */
void uart2_callback(uart_callback_args_t * p_args);

/*
 * 函数作用：
 *   FSP 为 uart7 生成的回调入口，由本模块统一接管。
 * 调用时机：
 *   不能手动调用，只会在 uart7 发生发送完成或接收完成事件时由底层自动触发。
 * 参数说明：
 *   p_args: FSP 传入的 UART 事件参数。
 * 返回值：
 *   无。
 * 调用方式：
 *   只需要保证函数名与 FSP 配置一致即可，上层不直接调用。
 */
void uart7_callback(uart_callback_args_t * p_args);

#endif /* DEBUG_UART_BSP_DEBUG_UART_H_ */
