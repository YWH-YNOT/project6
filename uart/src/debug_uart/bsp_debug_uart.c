#include "bsp_debug_uart.h"

/* 每个串口的运行状态都收口到这个结构体里，避免回调标志散落在全局变量中。 */
typedef struct st_bsp_debug_uart_channel
{
    /* 指向 FSP 生成的串口实例。 */
    uart_instance_t const * p_instance;
    /* 标记串口是否已经成功打开。 */
    volatile bool           is_open;
    /* 发送完成标志，由 UART_EVENT_TX_COMPLETE 置位。 */
    volatile bool           tx_done;
    /* 接收完成标志，由 UART_EVENT_RX_COMPLETE 置位。 */
    volatile bool           rx_done;
    /* 保存最近一次 UART 事件，后续如果要扩展错误处理可以直接利用。 */
    volatile uart_event_t   last_event;
} bsp_debug_uart_channel_t;

/* 板级层只维护 uart2、uart7 两个实例，后续如需扩串口，只需要在这里扩表。 */
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

static bsp_debug_uart_channel_t * bsp_debug_uart_get_channel (bsp_debug_uart_port_t port)
{
    /* 先做端口边界保护，避免上层误传导致数组越界。 */
    if (port >= BSP_DEBUG_UART_PORT_MAX)
    {
        return NULL;
    }

    return &g_bsp_debug_uart_channels[port];
}

static void bsp_debug_uart_handle_callback (bsp_debug_uart_port_t port, uart_callback_args_t * p_args)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);
    if ((NULL == p_channel) || (NULL == p_args))
    {
        return;
    }

    p_channel->last_event = p_args->event;

    /* 只在这里处理硬件完成事件，上层完全不需要了解 FSP 的事件细节。 */
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

static fsp_err_t bsp_debug_uart_wait_tx_complete (bsp_debug_uart_channel_t * p_channel)
{
    if (NULL == p_channel)
    {
        return FSP_ERR_ASSERTION;
    }

    /* 阻塞等待发送回调置位，形成简单可靠的同步发送语义。 */
    while (!p_channel->tx_done)
    {
        __NOP();
    }

    p_channel->tx_done = false;

    return FSP_SUCCESS;
}

static fsp_err_t bsp_debug_uart_wait_rx_complete (bsp_debug_uart_channel_t * p_channel)
{
    if (NULL == p_channel)
    {
        return FSP_ERR_ASSERTION;
    }

    /* 阻塞等待接收回调置位，适合没有 RTOS 的裸机轮询场景。 */
    while (!p_channel->rx_done)
    {
        __NOP();
    }

    p_channel->rx_done = false;

    return FSP_SUCCESS;
}

fsp_err_t BSP_DebugUart_Init (bsp_debug_uart_port_t port)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);
    fsp_err_t                  err       = FSP_SUCCESS;

    /* 空指针保护，防止非法端口传入。 */
    if (NULL == p_channel)
    {
        return FSP_ERR_ASSERTION;
    }

    /* 已经打开就不重复 open，避免 FSP 返回 ALREADY_OPEN。 */
    if (p_channel->is_open)
    {
        return FSP_SUCCESS;
    }

    /* 真正的硬件初始化仍然交给 FSP HAL，板级层只做统一收口。 */
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

fsp_err_t BSP_DebugUart_InitAll (void)
{
    /* 先开 uart2，再开 uart7，便于后续根据业务固定启动顺序。 */
    fsp_err_t err = BSP_DebugUart_Init(BSP_DEBUG_UART_PORT_2);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return BSP_DebugUart_Init(BSP_DEBUG_UART_PORT_7);
}

fsp_err_t BSP_DebugUart_Deinit (bsp_debug_uart_port_t port)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);
    fsp_err_t                  err       = FSP_SUCCESS;

    if (NULL == p_channel)
    {
        return FSP_ERR_ASSERTION;
    }

    /* 未打开时直接返回成功，保证上层调用更宽容。 */
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

bool BSP_DebugUart_IsOpen (bsp_debug_uart_port_t port)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);

    if (NULL == p_channel)
    {
        return false;
    }

    return p_channel->is_open;
}

fsp_err_t BSP_DebugUart_Write (bsp_debug_uart_port_t port, uint8_t const * p_data, uint32_t length)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);

    /* 数据指针和长度必须合法。 */
    if ((NULL == p_channel) || (NULL == p_data) || (0U == length))
    {
        return FSP_ERR_ASSERTION;
    }

    /* 未初始化时禁止收发，避免隐藏错误。 */
    if (!p_channel->is_open)
    {
        return FSP_ERR_NOT_OPEN;
    }

    /* 每次启动发送前清零完成标志，保证等待逻辑准确。 */
    p_channel->tx_done = false;

    return p_channel->p_instance->p_api->write(p_channel->p_instance->p_ctrl, p_data, length);
}

fsp_err_t BSP_DebugUart_Read (bsp_debug_uart_port_t port, uint8_t * p_data, uint32_t length)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);

    /* 数据指针和长度必须合法。 */
    if ((NULL == p_channel) || (NULL == p_data) || (0U == length))
    {
        return FSP_ERR_ASSERTION;
    }

    /* 未初始化时禁止收发，避免隐藏错误。 */
    if (!p_channel->is_open)
    {
        return FSP_ERR_NOT_OPEN;
    }

    /* 每次启动接收前清零完成标志，保证等待逻辑准确。 */
    p_channel->rx_done = false;

    return p_channel->p_instance->p_api->read(p_channel->p_instance->p_ctrl, p_data, length);
}

fsp_err_t BSP_DebugUart_WriteBlocking (bsp_debug_uart_port_t port, uint8_t const * p_data, uint32_t length)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);
    fsp_err_t                  err       = BSP_DebugUart_Write(port, p_data, length);

    /* 先发起发送，再等待回调，形成阻塞式发送接口。 */
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return bsp_debug_uart_wait_tx_complete(p_channel);
}

fsp_err_t BSP_DebugUart_ReadBlocking (bsp_debug_uart_port_t port, uint8_t * p_data, uint32_t length)
{
    bsp_debug_uart_channel_t * p_channel = bsp_debug_uart_get_channel(port);
    fsp_err_t                  err       = BSP_DebugUart_Read(port, p_data, length);

    /* 先发起接收，再等待回调，形成阻塞式接收接口。 */
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return bsp_debug_uart_wait_rx_complete(p_channel);
}

fsp_err_t BSP_DebugUart_ReadByteBlocking (bsp_debug_uart_port_t port, uint8_t * p_byte)
{
    /* 字节级读取是中间层解析字符串、浮点数和协议头的基础能力。 */
    return BSP_DebugUart_ReadBlocking(port, p_byte, 1U);
}

void uart2_callback (uart_callback_args_t * p_args)
{
    /* FSP 的 uart2 回调统一转交给板级层内部处理。 */
    bsp_debug_uart_handle_callback(BSP_DEBUG_UART_PORT_2, p_args);
}

void uart7_callback (uart_callback_args_t * p_args)
{
    /* FSP 的 uart7 回调统一转交给板级层内部处理。 */
    bsp_debug_uart_handle_callback(BSP_DEBUG_UART_PORT_7, p_args);
}
