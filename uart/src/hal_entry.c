#include "hal_data.h"

#include <stddef.h>

FSP_CPP_HEADER
void R_BSP_WarmStart(bsp_warm_start_event_t event);
FSP_CPP_FOOTER

#include "debug_uart/glove_frame.h"
#include "debug_uart/mid_uart.h"
#include "gesture/gesture_capture_service.h"
#include "gesture/gesture_service.h"
#include "key/mid_key_event.h"
#include "led/bsp_led.h"
#include "timer/mid_dispatch_timer.h"

#if GESTURE_SERVICE_CLASSIFY_PERIOD_MS != MID_DISPATCH_TIMER_CLASSIFY_PERIOD_MS
#error "手势业务层分类周期必须与定时器中间层分类节拍保持一致"
#endif

/*
 * 枚举作用：
 *   描述当前固件运行在哪条主链路上。
 * 说明：
 *   1. 识别模式负责“收帧 -> 分类 -> 发命令”；
 *   2. 采集模式负责“收帧 -> 导出 CSV”，两种模式严格互斥。
 */
typedef enum e_gesture_runtime_mode
{
    /* 默认上电进入识别模式，uart7 只发送 1 字节控制命令。 */
    GESTURE_RUNTIME_MODE_RECOGNIZE = 0,
    /* 按下 SW2 后进入采集模式，uart7 连续输出 10 列 CSV 特征文本。 */
    GESTURE_RUNTIME_MODE_COLLECT
} gesture_runtime_mode_t;

/* 当前系统运行模式，全局只保留一个状态源，便于主循环统一判断。 */
static gesture_runtime_mode_t g_gesture_runtime_mode = GESTURE_RUNTIME_MODE_RECOGNIZE;

/*
 * 应用层持续派发策略说明：
 * 1. 业务层在 100ms 分类周期里只负责产出“已经通过过滤的稳定命令”；
 * 2. 应用层把这份稳定命令锁存成“当前激活命令”，而不是只发一次就清空；
 * 3. 只要当前激活命令还有效，每个 200ms 发送窗口都会重复发它一次；
 * 4. 这样可以把“分类偶发抖动”与“串口发送节拍”解耦，避免用户感受到一卡一卡的断续发送。
 */
#define HAL_ENTRY_RELEASE_CONFIRM_COUNT    2U

static bool    g_hal_active_tx_valid          = false;
static uint8_t g_hal_active_tx_cmd            = 0U;
static uint8_t g_hal_release_confirm_count    = 0U;

/*
 * 内部函数作用：
 *   清空应用层当前锁存的“持续派发命令”状态。
 * 调用关系：
 *   在模式切换、稳定回到无效手势、或其他需要彻底停发命令的场景下调用。
 * 参数说明：
 *   无。
 * 返回值：
 *   无。
 */
static void hal_entry_clear_active_tx_command (void)
{
    g_hal_active_tx_valid       = false;
    g_hal_active_tx_cmd         = 0U;
    g_hal_release_confirm_count = 0U;
}

/*
 * 内部函数作用：
 *   根据一次新的 100ms 分类结果，更新应用层当前的持续派发命令状态。
 * 调用关系：
 *   仅在识别模式下、且本轮确实执行过分类后调用。
 * 参数说明：
 *   p_result: 本轮分类和过滤结果。
 * 返回值：
 *   无。
 * 处理策略说明：
 *   1. 如果业务层给出了 send_ready=true 的稳定命令，就立刻切换当前激活命令；
 *   2. 如果业务层还处于 send_hold=true 的确认阶段，就继续沿用旧命令，避免切换过程中断发；
 *   3. 如果连续两次分类都回到了 0x02/0x00 这类“无须发送”的结果，才真正停发当前激活命令；
 *   4. 这样既能保证同一方向命令持续以 200ms 频率重复发出，也能避免单次识别抖动造成的误停发。
 */
static void hal_entry_update_active_tx_command (gesture_service_result_t const * p_result)
{
    if ((NULL == p_result) || (!p_result->classify_ready))
    {
        return;
    }

    if (p_result->send_ready)
    {
        g_hal_active_tx_cmd         = p_result->send_cmd;
        g_hal_active_tx_valid       = true;
        g_hal_release_confirm_count = 0U;
        return;
    }

    if (p_result->send_hold)
    {
        g_hal_release_confirm_count = 0U;
        return;
    }

    if (p_result->suppress_common || (GESTURE_CMD_NONE == p_result->cmd))
    {
        if (g_hal_release_confirm_count < HAL_ENTRY_RELEASE_CONFIRM_COUNT)
        {
            g_hal_release_confirm_count++;
        }

        if (g_hal_release_confirm_count >= HAL_ENTRY_RELEASE_CONFIRM_COUNT)
        {
            hal_entry_clear_active_tx_command();
        }

        return;
    }

    g_hal_release_confirm_count = 0U;
}

/*
 * 内部函数作用：
 *   切换运行模式，并同步重置与模式相关的业务状态。
 * 调用关系：
 *   仅由 hal_entry_try_toggle_runtime_mode 调用。
 * 参数说明：
 *   mode: 目标运行模式。
 * 返回值：
 *   无。
 * 处理内容：
 *   1. 清空识别业务层的样本缓存、历史命令和确认次数；
 *   2. 清空应用层当前激活命令，避免旧模式遗留的命令串入新模式；
 *   3. 清空 100ms/200ms 调度节拍，避免旧模式遗留的节拍串入新模式；
 *   4. 根据模式更新 LED 状态，让现场调试时一眼可见当前模式。
 */
static void hal_entry_apply_runtime_mode (gesture_runtime_mode_t mode)
{
    GestureService_Init();
    hal_entry_clear_active_tx_command();
    MID_DispatchTimer_ClearDispatchFlags();

    g_gesture_runtime_mode = mode;

    if (GESTURE_RUNTIME_MODE_COLLECT == mode)
    {
        LED1_ON;
    }
    else
    {
        LED1_OFF;
    }
}

/*
 * 内部函数作用：
 *   轮询并处理一次按键模式切换请求。
 * 调用关系：
 *   在主循环中会多次调用，确保按键事件尽快生效。
 * 参数说明：
 *   无。
 * 返回值：
 *   无。
 * 调用方式：
 *   每次调用都会尝试消费一次按键事件；如果这次没有按下，就什么都不做。
 */
static void hal_entry_try_toggle_runtime_mode (void)
{
    if (!MID_KeyEvent_TryConsumeKey1Press())
    {
        return;
    }

    if (GESTURE_RUNTIME_MODE_RECOGNIZE == g_gesture_runtime_mode)
    {
        hal_entry_apply_runtime_mode(GESTURE_RUNTIME_MODE_COLLECT);
    }
    else
    {
        hal_entry_apply_runtime_mode(GESTURE_RUNTIME_MODE_RECOGNIZE);
    }
}

/*******************************************************************************************************************//**
 * 裸机工程主入口。
 *
 * 主流程说明：
 * 1. 启动后先初始化 LED、UART、按键事件层和 10ms 调度定时器；
 * 2. 默认进入识别模式；
 * 3. 主循环持续等待 uart2 收到一帧新的手套数据；
 * 4. 如果当前是采集模式，就把这帧转换成 CSV 文本发到 uart7；
 * 5. 如果当前是识别模式，就先做 50ms 采样；
 * 6. 当 100ms 分类窗口到点时，执行一次 SVM 分类并更新应用层当前的激活发送命令；
 * 7. 当 200ms 发送窗口到点时，只要当前仍存在激活命令，就重复从 uart7 发出 1 字节命令；
 * 8. 这样可以同时保证识别刷新更快、串口发送更平滑，不会出现用户感受到的“卡一下再发一下”。
 **********************************************************************************************************************/
void hal_entry (void)
{
    glove_frame_t            rx_frame;
    gesture_service_result_t gesture_result;
    fsp_err_t                err    = FSP_SUCCESS;
    bool                     classify_due = false;
    bool                     send_due     = false;
    uint8_t                  tx_cmd = 0U;

    /*
     * 启动阶段初始化顺序说明：
     * 1. 先拉起 LED，便于后续直接用灯指示模式；
     * 2. 再初始化串口，否则后续无法接收手套帧和输出命令/CSV；
     * 3. 再初始化按键事件和调度定时器；
     * 4. 最后明确进入识别模式，统一复位业务状态。
     */
    LED_Init();
    LED1_OFF;

    if (FSP_SUCCESS != MID_Uart_Init())
    {
        while (1)
        {
        }
    }

    if (FSP_SUCCESS != MID_KeyEvent_Init())
    {
        while (1)
        {
        }
    }

    if (FSP_SUCCESS != MID_DispatchTimer_Init())
    {
        while (1)
        {
        }
    }

    hal_entry_apply_runtime_mode(GESTURE_RUNTIME_MODE_RECOGNIZE);

    while (1)
    {
        /*
         * 第一步：尽早处理按键事件。
         * 这样如果用户刚按下 SW2，就能在等待下一帧之前先切模式。
         */
        hal_entry_try_toggle_runtime_mode();

        /*
         * 第二步：阻塞等待 uart2 上来一帧新的合法协议数据。
         * 这一步返回成功后，rx_frame 里就已经是可直接使用的结构化特征数据。
         */
        err = GloveFrame_Receive(MID_UART_PORT_2, &rx_frame);
        if (FSP_SUCCESS != err)
        {
            continue;
        }

        /*
         * 第三步：再补检查一次按键事件。
         * 这是为了覆盖“用户在等待串口帧期间按下 SW2”的情况，让模式切换不必额外多等一轮。
         */
        hal_entry_try_toggle_runtime_mode();

        if (GESTURE_RUNTIME_MODE_COLLECT == g_gesture_runtime_mode)
        {
            /*
             * 采集模式逻辑：
             * 1. 不做 SVM 分类；
             * 2. 不发送控制命令；
             * 3. 只把当前有效帧导出成 10 列 CSV；
             * 4. 顺手消费掉分类/发送标志，避免调试时把采集模式和识别模式的节拍状态混在一起。
             */
            (void) MID_DispatchTimer_TryConsumeClassifyFlag();
            (void) MID_DispatchTimer_TryConsumeSendFlag();

            err = GestureCaptureService_ProcessFrame(MID_UART_PORT_7, &rx_frame);
            if (FSP_SUCCESS != err)
            {
                continue;
            }

            continue;
        }

        /*
         * 识别模式逻辑的第一步：
         * 把当前帧交给业务层，按 50ms 节拍决定是否更新“最新样本缓存”。
         */
        err = GestureService_ProcessFrame(&rx_frame, &gesture_result);
        if (FSP_SUCCESS != err)
        {
            continue;
        }

        /*
         * 识别模式逻辑的第二步：
         * 只有当 100ms 分类标志到点，才允许真正执行一次新的分类周期。
         * 这一步不会直接发串口，而是只负责更新应用层当前的“激活发送命令”状态。
         */
        classify_due = MID_DispatchTimer_TryConsumeClassifyFlag();
        if (classify_due)
        {
            err = GestureService_RunClassifyCycle(&gesture_result);
            if (FSP_SUCCESS != err)
            {
                continue;
            }

            hal_entry_update_active_tx_command(&gesture_result);
        }

        /*
         * 识别模式逻辑的第四步：
         * 只有当 200ms 发送窗口到点，并且当前仍然存在激活命令时，
         * 才真正从 uart7 发出 1 字节控制命令。
         * 注意这里发完以后不会立刻清空命令，因为我们希望同一稳定方向能够持续重复发送。
         */
        send_due = MID_DispatchTimer_TryConsumeSendFlag();
        if (send_due && g_hal_active_tx_valid)
        {
            tx_cmd = g_hal_active_tx_cmd;
            LED1_TOGGLE;
            (void) MID_Uart_SendHex(MID_UART_PORT_7, &tx_cmd, 1U);
        }
    }
}

/*******************************************************************************************************************//**
 * 启动阶段回调。
 *
 * 运行时机说明：
 * 1. BSP_WARM_START_RESET 阶段发生在系统刚复位后；
 * 2. BSP_WARM_START_POST_C 阶段发生在 C 运行时环境建立完成后。
 *
 * 当前处理内容：
 * 1. 在 RESET 阶段使能数据 Flash 读取；
 * 2. 在 POST_C 阶段完成引脚复用配置，为 UART、LED 等外设正常工作做准备。
 *
 * @param[in] event 当前启动流程所在阶段
 **********************************************************************************************************************/
void R_BSP_WarmStart (bsp_warm_start_event_t event)
{
    if (BSP_WARM_START_RESET == event)
    {
#if BSP_FEATURE_FLASH_LP_VERSION != 0

        /* 允许读取数据 Flash，后续如果扩展参数区或模型区，可以直接复用这里的初始化。 */
        R_FACI_LP->DFLCTL = 1U;

        /*
         * 理论上这里需要等待 tDSTOP(6us)。
         * 但当前把使能动作放在更早的启动阶段，后续启动代码本身已经覆盖了这段等待时间。
         */
#endif
    }

    if (BSP_WARM_START_POST_C == event)
    {
        /* 只有完成 C 运行时和系统时钟初始化后，才可以安全配置引脚复用。 */
        R_IOPORT_Open(&IOPORT_CFG_CTRL, &IOPORT_CFG_NAME);

#if BSP_CFG_SDRAM_ENABLED

        /* 如果工程启用了 SDRAM，这里在引脚配置完成后继续初始化外部存储。 */
        R_BSP_SdramInit(true);
#endif
    }
}

#if BSP_TZ_SECURE_BUILD

FSP_CPP_HEADER
BSP_CMSE_NONSECURE_ENTRY void template_nonsecure_callable(void);

/* TrustZone 占位入口：若启用安全工程，构建阶段至少需要一个可供非安全域调用的接口。 */
BSP_CMSE_NONSECURE_ENTRY void template_nonsecure_callable(void)
{
}
FSP_CPP_FOOTER

#endif
