#include "gesture_service.h"

#include "gesture_model.h"

#include <stddef.h>
#include <string.h>

/*
 * 内部上下文说明：
 * 1. sampled_* 保存最近一次被 100ms 采样节拍选中的特征；
 * 2. history_cmd 和 confirm_count 用来实现“同一命令连续出现两次才真正发送”的过滤逻辑；
 * 3. last_classified_sample_count 用于避免 200ms 周期重复消费同一份旧样本。
 */
typedef struct st_gesture_service_context
{
    bool     has_sample;
    uint8_t  sampled_seq;
    uint8_t  sampled_status;
    uint8_t  history_cmd;
    uint8_t  confirm_count;
    uint32_t sampled_tick_ms;
    uint32_t last_sample_tick_ms;
    uint32_t sample_count;
    uint32_t classify_count;
    uint32_t last_classified_sample_count;
    int16_t  sampled_feature_raw[GESTURE_SERVICE_FEATURE_COUNT];
    float    sampled_feature[GESTURE_SERVICE_FEATURE_COUNT];
} gesture_service_context_t;

static gesture_service_context_t g_gesture_service_context;

/*
 * 内部函数作用：
 *   判断 now 与 last 之间是否已经走过指定毫秒周期，自动兼容无符号毫秒计数回绕。
 * 调用关系：
 *   仅用于 100ms 采样节拍判断。
 * 参数说明：
 *   now: 当前时间戳。
 *   last: 上一次采样时间戳。
 *   period_ms: 需要比较的周期。
 * 返回值：
 *   true 表示已经到期；
 *   false 表示尚未到期。
 */
static bool gesture_service_is_elapsed (uint32_t now, uint32_t last, uint32_t period_ms)
{
    return (now - last) >= period_ms;
}

/*
 * 内部函数作用：
 *   把输出结果结构体重置到干净初始状态。
 * 调用关系：
 *   每次业务接口开始处理前都会先调用。
 * 参数说明：
 *   p_result: 待重置的结果结构体。
 * 返回值：
 *   无。
 */
static void gesture_service_reset_result (gesture_service_result_t * p_result)
{
    if (NULL == p_result)
    {
        return;
    }

    (void) memset(p_result, 0, sizeof(*p_result));
    p_result->label = GM_INVALID_LABEL;
    p_result->cmd   = GESTURE_CMD_NONE;
}

/*
 * 内部函数作用：
 *   把当前协议帧复制进内部“最新样本缓存”。
 * 调用关系：
 *   当 100ms 采样条件满足时由 GestureService_ProcessFrame 调用。
 * 参数说明：
 *   p_frame: 当前被选中的协议帧。
 * 返回值：
 *   无。
 */
static void gesture_service_copy_sample_from_frame (glove_frame_t const * p_frame)
{
    g_gesture_service_context.sampled_seq         = p_frame->seq;
    g_gesture_service_context.sampled_status      = p_frame->status;
    g_gesture_service_context.sampled_tick_ms     = p_frame->tick_ms;
    g_gesture_service_context.last_sample_tick_ms = p_frame->tick_ms;
    g_gesture_service_context.sample_count++;

    (void) memcpy(g_gesture_service_context.sampled_feature_raw,
                  p_frame->feature_raw,
                  sizeof(g_gesture_service_context.sampled_feature_raw));
    (void) memcpy(g_gesture_service_context.sampled_feature,
                  p_frame->feature,
                  sizeof(g_gesture_service_context.sampled_feature));
}

/*
 * 内部函数作用：
 *   把内部缓存里的当前样本信息填充到输出结果结构体。
 * 调用关系：
 *   在采样更新或分类输出时调用，让应用层可以看到当前参与处理的样本。
 * 参数说明：
 *   p_result: 输出结果结构体。
 * 返回值：
 *   无。
 */
static void gesture_service_fill_result_from_sample (gesture_service_result_t * p_result)
{
    p_result->sample_seq     = g_gesture_service_context.sampled_seq;
    p_result->sample_status  = g_gesture_service_context.sampled_status;
    p_result->sample_tick_ms = g_gesture_service_context.sampled_tick_ms;
    p_result->sample_count   = g_gesture_service_context.sample_count;
    p_result->classify_count = g_gesture_service_context.classify_count;
    p_result->history_cmd    = g_gesture_service_context.history_cmd;
    p_result->confirm_count  = g_gesture_service_context.confirm_count;

    (void) memcpy(p_result->feature_raw,
                  g_gesture_service_context.sampled_feature_raw,
                  sizeof(p_result->feature_raw));
    (void) memcpy(p_result->feature,
                  g_gesture_service_context.sampled_feature,
                  sizeof(p_result->feature));
}

/*
 * 内部函数作用：
 *   把模型输出标签映射成对外命令字节。
 * 调用关系：
 *   仅在分类完成后调用。
 * 参数说明：
 *   label: SVM 输出的类别标签。
 * 返回值：
 *   返回映射后的命令字节；如果标签非法，则返回 GESTURE_CMD_NONE。
 */
static uint8_t gesture_service_map_command (uint8_t label)
{
    /*
     * 映射表沿用原 STM32 版本的 gr_cmd_map。
     * 这样迁移到 RA6M5 后，对外发送的控制语义不发生变化。
     */
    static const uint8_t g_cmd_map[GM_N_CLASSES] =
    {
        GESTURE_CMD_COMMON, /* fist  */
        GESTURE_CMD_COMMON, /* open  */
        GESTURE_CMD_ONE,    /* one   */
        GESTURE_CMD_COMMON, /* two   */
        GESTURE_CMD_ROCK,   /* rock  */
        GESTURE_CMD_UP,     /* up    */
        GESTURE_CMD_DOWN,   /* down  */
        GESTURE_CMD_LEFT,   /* left  */
        GESTURE_CMD_RIGHT   /* right */
    };

    if (label >= GM_N_CLASSES)
    {
        return GESTURE_CMD_NONE;
    }

    return g_cmd_map[label];
}

/*
 * 内部函数作用：
 *   对本次分类得到的命令执行发送过滤。
 * 调用关系：
 *   仅在完成分类后调用。
 * 参数说明：
 *   p_result: 输出结果结构体，同时会被写入过滤后的 send_ready、send_cmd 等状态。
 * 返回值：
 *   无。
 * 过滤逻辑说明：
 *   1. 如果命令是 0x02 或 0x00，直接压制，不对外发送；
 *   2. 如果命令与上一次候选命令相同，则确认次数加 1；
 *   3. 只有同一候选命令连续达到 2 次，才真正允许发送。
 */
static void gesture_service_apply_send_filter (gesture_service_result_t * p_result)
{
    uint8_t cmd = p_result->cmd;

    if ((GESTURE_CMD_COMMON == cmd) || (GESTURE_CMD_NONE == cmd))
    {
        p_result->suppress_common = (GESTURE_CMD_COMMON == cmd);
        g_gesture_service_context.history_cmd   = GESTURE_CMD_NONE;
        g_gesture_service_context.confirm_count = 0U;
        p_result->history_cmd                   = GESTURE_CMD_NONE;
        p_result->confirm_count                 = 0U;
        return;
    }

    if (cmd == g_gesture_service_context.history_cmd)
    {
        if (g_gesture_service_context.confirm_count < UINT8_MAX)
        {
            g_gesture_service_context.confirm_count++;
        }
    }
    else
    {
        g_gesture_service_context.history_cmd   = cmd;
        g_gesture_service_context.confirm_count = 1U;
    }

    if (g_gesture_service_context.confirm_count >= GESTURE_SERVICE_SEND_CONFIRM_COUNT)
    {
        g_gesture_service_context.confirm_count = GESTURE_SERVICE_SEND_CONFIRM_COUNT;
        p_result->send_ready                    = true;
        p_result->send_cmd                      = cmd;
    }
    else
    {
        p_result->send_hold = true;
    }

    p_result->history_cmd   = g_gesture_service_context.history_cmd;
    p_result->confirm_count = g_gesture_service_context.confirm_count;
}

/*
 * 函数作用：
 *   清空识别业务层内部状态。
 * 参数说明：
 *   无。
 * 返回值：
 *   无。
 * 调用方式：
 *   系统启动和模式切换时调用，相当于重新开始一次识别流程。
 */
void GestureService_Init (void)
{
    (void) memset(&g_gesture_service_context, 0, sizeof(g_gesture_service_context));
}

/*
 * 函数作用：
 *   处理一帧新数据，并在满足 100ms 周期时刷新样本缓存。
 * 参数说明：
 *   p_frame: 当前协议帧。
 *   p_result: 输出结果结构体。
 * 返回值：
 *   FSP_SUCCESS 表示处理完成；
 *   FSP_ERR_ASSERTION 表示输入参数为空。
 * 调用方式：
 *   每收到一帧合法协议数据后调用一次；它只负责更新样本，不负责执行分类。
 */
fsp_err_t GestureService_ProcessFrame (glove_frame_t const * p_frame, gesture_service_result_t * p_result)
{
    if ((NULL == p_frame) || (NULL == p_result))
    {
        return FSP_ERR_ASSERTION;
    }

    gesture_service_reset_result(p_result);

    /*
     * 只有协议层已经确认 data_valid 的帧，才允许进入样本缓存。
     * 这样可以避免异常帧、占位帧和无效状态污染分类输入。
     */
    if (0U == (p_frame->status & GLOVE_FRAME_STATUS_DATA_VALID))
    {
        return FSP_SUCCESS;
    }

    if ((!g_gesture_service_context.has_sample) ||
        gesture_service_is_elapsed(p_frame->tick_ms,
                                   g_gesture_service_context.last_sample_tick_ms,
                                   GESTURE_SERVICE_SAMPLE_PERIOD_MS))
    {
        gesture_service_copy_sample_from_frame(p_frame);
        g_gesture_service_context.has_sample = true;
        p_result->sample_updated             = true;
        gesture_service_fill_result_from_sample(p_result);
    }

    return FSP_SUCCESS;
}

/*
 * 函数作用：
 *   在 200ms 调度周期到来时，使用当前最新样本执行一次 SVM 分类和命令过滤。
 * 参数说明：
 *   p_result: 输出结果结构体。
 * 返回值：
 *   FSP_SUCCESS 表示本次周期已处理完成；
 *   FSP_ERR_ASSERTION 表示结果指针为空。
 * 调用方式：
 *   只有当 MID_DispatchTimer_TryConsumeDispatchFlag 返回 true 时，应用层才应该调用本函数。
 */
fsp_err_t GestureService_RunClassifyCycle (gesture_service_result_t * p_result)
{
    uint8_t label;

    if (NULL == p_result)
    {
        return FSP_ERR_ASSERTION;
    }

    gesture_service_reset_result(p_result);

    if (!g_gesture_service_context.has_sample)
    {
        return FSP_SUCCESS;
    }

    /*
     * 如果 200ms 周期先到了，但 100ms 没有选出新样本，就跳过本轮分类，
     * 避免同一份旧样本被反复重复分类。
     */
    if (g_gesture_service_context.sample_count == g_gesture_service_context.last_classified_sample_count)
    {
        return FSP_SUCCESS;
    }

    label = GestureModel_Classify(g_gesture_service_context.sampled_feature);

    g_gesture_service_context.classify_count++;
    g_gesture_service_context.last_classified_sample_count = g_gesture_service_context.sample_count;

    p_result->classify_ready = true;
    p_result->label          = label;
    p_result->cmd            = gesture_service_map_command(label);

    gesture_service_fill_result_from_sample(p_result);
    p_result->classify_count = g_gesture_service_context.classify_count;
    gesture_service_apply_send_filter(p_result);

    return FSP_SUCCESS;
}
