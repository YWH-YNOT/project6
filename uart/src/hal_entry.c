#include "hal_data.h"

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
 *   2. 清空 200ms 调度节拍，避免旧模式遗留的节拍串入新模式；
 *   3. 根据模式更新 LED 状态，让现场调试时一眼可见当前模式。
 */
static void hal_entry_apply_runtime_mode (gesture_runtime_mode_t mode)
{
    GestureService_Init();
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
 * 1. 启动后先初始化 LED、UART、按键事件层和 200ms 调度定时器；
 * 2. 默认进入识别模式；
 * 3. 主循环持续等待 uart2 收到一帧新的手套数据；
 * 4. 如果当前是采集模式，就把这帧转换成 CSV 文本发到 uart7；
 * 5. 如果当前是识别模式，就先做 100ms 采样，再在 200ms 周期到点时做一次 SVM 分类；
 * 6. 只有分类结果通过二次确认过滤后，才真正从 uart7 发出 1 字节命令。
 **********************************************************************************************************************/
void hal_entry (void)
{
    glove_frame_t            rx_frame;
    gesture_service_result_t gesture_result;
    fsp_err_t                err    = FSP_SUCCESS;
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
             * 4. 顺手消费掉调度标志，避免退出采集模式后突然补跑旧的 200ms 周期。
             */
            (void) MID_DispatchTimer_TryConsumeDispatchFlag();

            err = GestureCaptureService_ProcessFrame(MID_UART_PORT_7, &rx_frame);
            if (FSP_SUCCESS != err)
            {
                continue;
            }

            continue;
        }

        /*
         * 识别模式逻辑的第一步：
         * 把当前帧交给业务层，按 100ms 节拍决定是否更新“最新样本缓存”。
         */
        err = GestureService_ProcessFrame(&rx_frame, &gesture_result);
        if (FSP_SUCCESS != err)
        {
            continue;
        }

        /*
         * 识别模式逻辑的第二步：
         * 只有当 200ms 调度标志到点，才允许真正执行一次分类周期。
         */
        if (MID_DispatchTimer_TryConsumeDispatchFlag())
        {
            err = GestureService_RunClassifyCycle(&gesture_result);
            if (FSP_SUCCESS != err)
            {
                continue;
            }

            /*
             * 识别模式逻辑的第三步：
             * 只有当业务层确认 send_ready 为 true，才说明：
             * 1. 本轮确实完成了一次新分类；
             * 2. 结果不是被压制的 0x02/0x00；
             * 3. 命令已经通过了二次确认过滤。
             */
            if (gesture_result.send_ready)
            {
                tx_cmd = gesture_result.send_cmd;
                LED1_TOGGLE;
                (void) MID_Uart_SendHex(MID_UART_PORT_7, &tx_cmd, 1U);
            }
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
