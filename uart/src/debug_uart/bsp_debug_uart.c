#include "bsp_debug_uart.h"

/*
 * 内部上下文说明：
 * 1. 每个串口都有一份独立运行状态，避免把发送完成、接收完成等标志散落成多个全局变量。
 * 2. is_open 表示底层 open 是否成功；
 * 3. tx_done/rx_done 由回调置位，供阻塞接口轮询等待；
 * 4. last_event 预留给后续错误排查和调试扩展使用。
 */
typedef struct st_bsp_debug_uart_channel
{
    uart_instance_t const * p_instance;
    volatile bool           is_open;
    volatile bool           tx_done;
    volatile bool           rx_done;
    volatile uart_event_t   last_event;
} bsp_debug_uart_channel_t;

/*
 * 模块内只维护 uart2 和 uart7 两个硬件实例。
 * 如果后续需要扩展新的串口，只需要在这里扩表，并补上对应回调即可。
 */
static bsp_debug_uart_channel_t g_bsp_debug_uart_channels[BSP_DEBUG_UART_PORT_MAX] =
{
    [BSP_DEBUG_UART_PORT_2] =
    {
        .p_instance = &g_uart2,
        .is_open    = false,
        .tx_done    = false,
        .rx_done    = false,
        .last_event = 0
    },
    [BSP_DEBUG_UART_PORT_7] =
    {
        .p_instance = &g_uart7,
        .is_open    = false,
        .tx_done    = false,
        .rx_done    = false,
        .last_event = 0
    }
};

/*
 * 内部函数作用：
 *   根据逻辑串口号找到对应的运行上下文。
 * 调用关系：
 *   仅供本文件内的初始化、收发、回调函数使用。
 * 参数说明：
 *   port: 逻辑串口号。
 * 返回值：
 *   成功时返回上下文指针；
 *   如果端口超范围，返回 NULL。
 */
static bsp_debug_uart_channel_t * bsp_debug_uart_get_channel (bsp_debug_uart_port_t port)
{
    if (port >= BSP_DEBUG_UART_PORT_MAX)
    {
        return NULL;
    }

    return &g_bsp_debug_uart_channels[port];
}

/*
 * 内部函数作用：
 *   统一处理 uart2/uart7 的底层回调，把 FSP 事件转换成本模块自己的完成标志。
 * 调用关系：
 *   由 uart2_callback、uart7_callback 间接调用。
 * 参数说明：
 *   port: 触发回调的逻辑串口号。
 *   p_args: FSP 传入的回调事件参数。
 * 返回值：
 *   无。
 */
static void bsp_debug_uart_handle_callback (bsp_debug_uart_port_t port, uart_callback_args_t * p_args)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);
    if ((NULL == p_channel) || (NULL == p_args))
    {
        return;
    }

    p_channel->last_event = p_args->event;

    /*
     * 这里只做最小化处理：
     * 1. 记录最近一次事件；
     * 2. 对发送完成和接收完成事件置位标志；
     * 3. 不在回调里做任何业务逻辑，保持中断路径尽量短。
     */
    switch (p_args->event)
    {
        case UART_EVENT_TX_COMPLETE:
        {
            p_channel->tx_done = true;
            break;
        }

        case UART_EVENT_RX_COMPLETE:
        {
            p_channel->rx_done = true;
            break;
        }

        default:
        {
            break;
        }
    }
}

/*
 * 内部函数作用：
 *   阻塞等待一次发送完成。
 * 调用关系：
 *   仅由 BSP_DebugUart_WriteBlocking 调用。
 * 参数说明：
 *   p_channel: 已经发起发送的串口上下文。
 * 返回值：
 *   FSP_SUCCESS 表示等待完成；
 *   FSP_ERR_ASSERTION 表示上下文为空。
 */
static fsp_err_t bsp_debug_uart_wait_tx_complete (bsp_debug_uart_channel_t * p_channel)
{
    if (NULL == p_channel)
    {
        return FSP_ERR_ASSERTION;
    }

    while (!p_channel->tx_done)
    {
        __NOP();
    }

    p_channel->tx_done = false;

    return FSP_SUCCESS;
}

/*
 * 内部函数作用：
 *   阻塞等待一次接收完成。
 * 调用关系：
 *   仅由 BSP_DebugUart_ReadBlocking 调用。
 * 参数说明：
 *   p_channel: 已经发起接收的串口上下文。
 * 返回值：
 *   FSP_SUCCESS 表示等待完成；
 *   FSP_ERR_ASSERTION 表示上下文为空。
 */
static fsp_err_t bsp_debug_uart_wait_rx_complete (bsp_debug_uart_channel_t * p_channel)
{
    if (NULL == p_channel)
    {
        return FSP_ERR_ASSERTION;
    }

    while (!p_channel->rx_done)
    {
        __NOP();
    }

    p_channel->rx_done = false;

    return FSP_SUCCESS;
}

/*
 * 函数作用：
 *   打开指定串口并初始化运行状态。
 * 参数说明：
 *   port: 逻辑串口号。
 * 返回值：
 *   FSP_SUCCESS 表示成功；
 *   其他错误码表示端口非法或底层 open 失败。
 * 调用方式：
 *   由中间层在系统启动时调用，正常情况下不会频繁重复执行。
 */
fsp_err_t BSP_DebugUart_Init (bsp_debug_uart_port_t port)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);
    fsp_err_t                  err       = FSP_SUCCESS;

    if (NULL == p_channel)
    {
        return FSP_ERR_ASSERTION;
    }

    if (p_channel->is_open)
    {
        return FSP_SUCCESS;
    }

    err = p_channel->p_instance->p_api->open(p_channel->p_instance->p_ctrl, p_channel->p_instance->p_cfg);
    if (FSP_SUCCESS == err)
    {
        p_channel->is_open    = true;
        p_channel->tx_done    = false;
        p_channel->rx_done    = false;
        p_channel->last_event = 0;
    }

    return err;
}

/*
 * 函数作用：
 *   统一初始化 uart2 和 uart7。
 * 参数说明：
 *   无。
 * 返回值：
 *   FSP_SUCCESS 表示两个串口都初始化完成；
 *   其他错误码表示某一步初始化失败。
 * 调用方式：
 *   当前工程上层统一调用这个函数，不需要分别初始化两个串口。
 */
fsp_err_t BSP_DebugUart_InitAll (void)
{
    fsp_err_t err = BSP_DebugUart_Init(BSP_DEBUG_UART_PORT_2);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return BSP_DebugUart_Init(BSP_DEBUG_UART_PORT_7);
}

/*
 * 函数作用：
 *   关闭指定串口。
 * 参数说明：
 *   port: 需要关闭的逻辑串口号。
 * 返回值：
 *   FSP_SUCCESS 表示关闭成功或原本未打开；
 *   其他错误码表示关闭失败。
 * 调用方式：
 *   当前工程一般不主动调用，仅为后续扩展预留。
 */
fsp_err_t BSP_DebugUart_Deinit (bsp_debug_uart_port_t port)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);
    fsp_err_t                  err       = FSP_SUCCESS;

    if (NULL == p_channel)
    {
        return FSP_ERR_ASSERTION;
    }

    if (!p_channel->is_open)
    {
        return FSP_SUCCESS;
    }

    err = p_channel->p_instance->p_api->close(p_channel->p_instance->p_ctrl);
    if (FSP_SUCCESS == err)
    {
        p_channel->is_open = false;
    }

    return err;
}

/*
 * 函数作用：
 *   查询指定串口是否已经可用。
 * 参数说明：
 *   port: 需要查询的逻辑串口号。
 * 返回值：
 *   true 表示串口已打开；
 *   false 表示串口未打开或端口非法。
 * 调用方式：
 *   用于上层做状态判断，不会改变任何运行状态。
 */
bool BSP_DebugUart_IsOpen (bsp_debug_uart_port_t port)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);

    if (NULL == p_channel)
    {
        return false;
    }

    return p_channel->is_open;
}

/*
 * 函数作用：
 *   发起一次非阻塞发送。
 * 参数说明：
 *   port: 目标串口号。
 *   p_data: 待发送数据首地址。
 *   length: 待发送字节数。
 * 返回值：
 *   FSP_SUCCESS 表示发送任务已成功提交；
 *   其他错误码表示参数非法、串口未打开或底层写入失败。
 * 调用方式：
 *   如果需要同步等待完成，请改用 BSP_DebugUart_WriteBlocking。
 */
fsp_err_t BSP_DebugUart_Write (bsp_debug_uart_port_t port, uint8_t const * p_data, uint32_t length)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);

    if ((NULL == p_channel) || (NULL == p_data) || (0U == length))
    {
        return FSP_ERR_ASSERTION;
    }

    if (!p_channel->is_open)
    {
        return FSP_ERR_NOT_OPEN;
    }

    p_channel->tx_done = false;

    return p_channel->p_instance->p_api->write(p_channel->p_instance->p_ctrl, p_data, length);
}

/*
 * 函数作用：
 *   发起一次非阻塞接收。
 * 参数说明：
 *   port: 来源串口号。
 *   p_data: 接收缓冲区。
 *   length: 期望接收的字节数。
 * 返回值：
 *   FSP_SUCCESS 表示接收任务已成功提交；
 *   其他错误码表示参数非法、串口未打开或底层读启动失败。
 * 调用方式：
 *   如果需要同步等待完整数据，请改用 BSP_DebugUart_ReadBlocking。
 */
fsp_err_t BSP_DebugUart_Read (bsp_debug_uart_port_t port, uint8_t * p_data, uint32_t length)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);

    if ((NULL == p_channel) || (NULL == p_data) || (0U == length))
    {
        return FSP_ERR_ASSERTION;
    }

    if (!p_channel->is_open)
    {
        return FSP_ERR_NOT_OPEN;
    }

    p_channel->rx_done = false;

    return p_channel->p_instance->p_api->read(p_channel->p_instance->p_ctrl, p_data, length);
}

/*
 * 函数作用：
 *   发送数据并同步等待发送完成。
 * 参数说明：
 *   port: 目标串口号。
 *   p_data: 待发送数据首地址。
 *   length: 数据长度。
 * 返回值：
 *   FSP_SUCCESS 表示已经发送完成；
 *   其他错误码表示发起发送失败或等待上下文无效。
 * 调用方式：
 *   当前裸机工程中，命令字节发送和 CSV 文本发送都直接使用本函数。
 */
fsp_err_t BSP_DebugUart_WriteBlocking (bsp_debug_uart_port_t port, uint8_t const * p_data, uint32_t length)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);
    fsp_err_t                  err       = BSP_DebugUart_Write(port, p_data, length);

    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return bsp_debug_uart_wait_tx_complete(p_channel);
}

/*
 * 函数作用：
 *   接收固定长度数据并同步等待接收完成。
 * 参数说明：
 *   port: 来源串口号。
 *   p_data: 接收缓冲区。
 *   length: 需要接收的字节数。
 * 返回值：
 *   FSP_SUCCESS 表示已经收满指定长度；
 *   其他错误码表示发起接收失败或等待上下文无效。
 * 调用方式：
 *   协议层按长度收完整帧时通过本函数工作。
 */
fsp_err_t BSP_DebugUart_ReadBlocking (bsp_debug_uart_port_t port, uint8_t * p_data, uint32_t length)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);
    fsp_err_t                  err       = BSP_DebugUart_Read(port, p_data, length);

    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return bsp_debug_uart_wait_rx_complete(p_channel);
}

/*
 * 函数作用：
 *   阻塞读取单个字节。
 * 参数说明：
 *   port: 来源串口号。
 *   p_byte: 单字节输出地址。
 * 返回值：
 *   FSP_SUCCESS 表示成功读到 1 个字节；
 *   其他错误码表示底层接收失败。
 * 调用方式：
 *   协议同步、逐字节找帧头等场景通过本函数简化调用。
 */
fsp_err_t BSP_DebugUart_ReadByteBlocking (bsp_debug_uart_port_t port, uint8_t * p_byte)
{
    return BSP_DebugUart_ReadBlocking(port, p_byte, 1U);
}

/*
 * 函数作用：
 *   处理 uart2 的 FSP 回调事件。
 * 参数说明：
 *   p_args: FSP 回调参数。
 * 返回值：
 *   无。
 * 调用方式：
 *   不能手动调用，只能由 FSP 底层在 uart2 事件到来时自动触发。
 */
void uart2_callback (uart_callback_args_t * p_args)
{
    bsp_debug_uart_handle_callback(BSP_DEBUG_UART_PORT_2, p_args);
}

/*
 * 函数作用：
 *   处理 uart7 的 FSP 回调事件。
 * 参数说明：
 *   p_args: FSP 回调参数。
 * 返回值：
 *   无。
 * 调用方式：
 *   不能手动调用，只能由 FSP 底层在 uart7 事件到来时自动触发。
 */
void uart7_callback (uart_callback_args_t * p_args)
{
    bsp_debug_uart_handle_callback(BSP_DEBUG_UART_PORT_7, p_args);
}
