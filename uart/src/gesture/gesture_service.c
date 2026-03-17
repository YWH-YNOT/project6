#include "gesture_service.h"

#include "gesture_model.h"

#include <stddef.h>
#include <string.h>

/*
 * 特征名字仅用于串口调试。
 * 与 STM32 发包顺序保持一致，便于联调时逐项核对。
 */
static char const * const g_gesture_feature_names[GESTURE_SERVICE_FEATURE_COUNT] =
{
    "f0x",
    "f0y",
    "f1x",
    "f1y",
    "f2x",
    "f2y",
    "f3x",
    "f3y",
    "roll",
    "pitch"
};

/*
 * 内部上下文：
 * 1. sampled_* 保存最近一次 100ms 采样得到的分类输入；
 * 2. history_cmd / confirm_count 复刻 STM32 端的发送过滤逻辑；
 * 3. last_classified_sample_count 用于避免定时器连续消费同一份旧样本。
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

static bool gesture_service_is_elapsed (uint32_t now, uint32_t last, uint32_t period_ms)
{
    /* 使用无符号减法，天然支持 32 位毫秒计数回绕。 */
    return (now - last) >= period_ms;
}

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

static uint8_t gesture_service_map_command (uint8_t label)
{
    /*
     * 这一张映射表严格沿用 STM32 control.c 里的 gr_cmd_map。
     * 先保证迁移后一致，再谈后续手势语义优化。
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

static void gesture_service_apply_send_filter (gesture_service_result_t * p_result)
{
    uint8_t cmd = p_result->cmd;

    /*
     * 0x02 是用户明确要求屏蔽的公共指令。
     * 0x00 则表示本次没有有效动作，也不应继续向下派发。
     */
    if ((GESTURE_CMD_COMMON == cmd) || (GESTURE_CMD_NONE == cmd))
    {
        p_result->suppress_common = (GESTURE_CMD_COMMON == cmd);
        g_gesture_service_context.history_cmd  = GESTURE_CMD_NONE;
        g_gesture_service_context.confirm_count = 0U;
        p_result->history_cmd                  = GESTURE_CMD_NONE;
        p_result->confirm_count               = 0U;
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

static char const * gesture_service_get_label_name (uint8_t label)
{
    if (label >= GM_N_CLASSES)
    {
        return "NONE";
    }

    return gm_labels[label];
}

static uint32_t gesture_service_write_unsigned (uint32_t value, char * p_buffer)
{
    char     temp[10];
    uint32_t count = 0U;
    uint32_t index = 0U;

    if (0U == value)
    {
        p_buffer[0] = '0';
        return 1U;
    }

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

static fsp_err_t gesture_service_send_text (mid_uart_port_t tx_port, char const * p_text)
{
    if (NULL == p_text)
    {
        return FSP_ERR_ASSERTION;
    }

    return MID_Uart_SendString(tx_port, p_text);
}

static fsp_err_t gesture_service_send_uint32 (mid_uart_port_t tx_port, uint32_t value)
{
    char     buffer[11];
    uint32_t length = gesture_service_write_unsigned(value, buffer);

    return MID_Uart_SendBytes(tx_port, (uint8_t const *) buffer, length);
}

static fsp_err_t gesture_service_send_hex_nibble (mid_uart_port_t tx_port, uint8_t value)
{
    uint8_t ascii_code;
    char    ch;

    if (value < 10U)
    {
        ascii_code = (uint8_t) ('0' + value);
    }
    else
    {
        ascii_code = (uint8_t) ('A' + (value - 10U));
    }

    ch = (char) ascii_code;

    return MID_Uart_SendBytes(tx_port, (uint8_t const *) &ch, 1U);
}

static fsp_err_t gesture_service_send_hex8 (mid_uart_port_t tx_port, uint8_t value)
{
    fsp_err_t err = gesture_service_send_hex_nibble(tx_port, (uint8_t) ((value >> 4) & 0x0FU));
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return gesture_service_send_hex_nibble(tx_port, (uint8_t) (value & 0x0FU));
}

void GestureService_Init (void)
{
    (void) memset(&g_gesture_service_context, 0, sizeof(g_gesture_service_context));
}

fsp_err_t GestureService_ProcessFrame (glove_frame_t const * p_frame, gesture_service_result_t * p_result)
{
    if ((NULL == p_frame) || (NULL == p_result))
    {
        return FSP_ERR_ASSERTION;
    }

    gesture_service_reset_result(p_result);

    /*
     * 只有 data_valid 置位时，才允许这帧进入样本缓存。
     * 这样可以避免异常帧、占位帧把分类输入污染掉。
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
     * 如果 200ms 定时器先于 100ms 新样本到来，就直接跳过本次周期，
     * 避免反复对同一份旧数据重复分类与发指令。
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

fsp_err_t GestureService_PrintResult (mid_uart_port_t tx_port, gesture_service_result_t const * p_result)
{
    fsp_err_t err;

    if (NULL == p_result)
    {
        return FSP_ERR_ASSERTION;
    }

    if (!p_result->classify_ready)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    err = gesture_service_send_text(tx_port, "gesture seq=");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_uint32(tx_port, p_result->sample_seq);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_text(tx_port, " tick=");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_uint32(tx_port, p_result->sample_tick_ms);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_text(tx_port, " status=0x");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_hex8(tx_port, p_result->sample_status);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_text(tx_port, " sample=");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_uint32(tx_port, p_result->sample_count);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_text(tx_port, " classify=");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_uint32(tx_port, p_result->classify_count);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_text(tx_port, " label=");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_uint32(tx_port, p_result->label);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_text(tx_port, "(");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_text(tx_port, gesture_service_get_label_name(p_result->label));
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_text(tx_port, ") cmd=0x");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_hex8(tx_port, p_result->cmd);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_text(tx_port, " hist=0x");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_hex8(tx_port, p_result->history_cmd);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_text(tx_port, " confirm=");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_uint32(tx_port, p_result->confirm_count);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gesture_service_send_text(tx_port, " tx=");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (p_result->send_ready)
    {
        err = gesture_service_send_text(tx_port, "0x");
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        err = gesture_service_send_hex8(tx_port, p_result->send_cmd);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }
    else if (p_result->suppress_common)
    {
        err = gesture_service_send_text(tx_port, "skip(0x02)");
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }
    else if (p_result->send_hold)
    {
        err = gesture_service_send_text(tx_port, "hold");
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }
    else
    {
        err = gesture_service_send_text(tx_port, "skip");
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    err = gesture_service_send_text(tx_port, "\r\nfeat=[");
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    for (uint32_t i = 0U; i < GESTURE_SERVICE_FEATURE_COUNT; i++)
    {
        err = gesture_service_send_text(tx_port, g_gesture_feature_names[i]);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        err = gesture_service_send_text(tx_port, "=");
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        err = MID_Uart_SendFloatText(tx_port, p_result->feature[i], 2U, false);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        if (i < (GESTURE_SERVICE_FEATURE_COUNT - 1U))
        {
            err = gesture_service_send_text(tx_port, ", ");
            if (FSP_SUCCESS != err)
            {
                return err;
            }
        }
    }

    return gesture_service_send_text(tx_port, "]\r\n\r\n");
}
