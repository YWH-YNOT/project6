#include "gesture_capture_service.h"

#include <stddef.h>
#include <string.h>

typedef struct st_gesture_capture_service_context
{
    uint32_t exported_count;
} gesture_capture_service_context_t;

static gesture_capture_service_context_t g_gesture_capture_service_context;

static fsp_err_t gesture_capture_service_send_separator(mid_uart_port_t tx_port, bool is_last)
{
    if (is_last)
    {
        return MID_Uart_SendString(tx_port, "\r\n");
    }

    return MID_Uart_SendString(tx_port, ",");
}

void GestureCaptureService_Init(void)
{
    (void) memset(&g_gesture_capture_service_context, 0, sizeof(g_gesture_capture_service_context));
}

fsp_err_t GestureCaptureService_ProcessFrame(mid_uart_port_t tx_port, glove_frame_t const * p_frame)
{
    fsp_err_t err = FSP_SUCCESS;

    if (NULL == p_frame)
    {
        return FSP_ERR_ASSERTION;
    }

    /*
     * 采集链路只导出已经被协议层判定为有效的数据。
     * 这样可以避免 CRC 错包、占位帧或无效状态污染训练集。
     */
    if (0U == (p_frame->status & GLOVE_FRAME_STATUS_DATA_VALID))
    {
        return FSP_SUCCESS;
    }

    for (uint32_t i = 0U; i < GLOVE_FRAME_FEATURE_COUNT; i++)
    {
        err = MID_Uart_SendFloatText(tx_port, p_frame->feature[i], 2U, false);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        err = gesture_capture_service_send_separator(tx_port, (i + 1U) == GLOVE_FRAME_FEATURE_COUNT);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    g_gesture_capture_service_context.exported_count++;

    return FSP_SUCCESS;
}
