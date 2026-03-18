#include "gesture_capture_service.h"

#include <stddef.h>

/*
 * 内部函数作用：
 *   在导出 CSV 时发送列分隔符或行结束符。
 * 调用关系：
 *   仅由 GestureCaptureService_ProcessFrame 使用。
 * 参数说明：
 *   tx_port: 输出串口号。
 *   is_last: 为 true 表示当前已经是最后一列，需要发送换行；否则发送逗号。
 * 返回值：
 *   FSP_SUCCESS 表示发送成功；
 *   其他错误码透传自字符串发送接口。
 */
static fsp_err_t gesture_capture_service_send_separator (mid_uart_port_t tx_port, bool is_last)
{
    if (is_last)
    {
        return MID_Uart_SendString(tx_port, "\r\n");
    }

    return MID_Uart_SendString(tx_port, ",");
}

/*
 * 函数作用：
 *   把一帧协议数据导出成一行 CSV 特征文本。
 * 参数说明：
 *   tx_port: 文本输出串口。
 *   p_frame: 当前协议帧。
 * 返回值：
 *   FSP_SUCCESS 表示处理完成；
 *   FSP_ERR_ASSERTION 表示帧指针为空；
 *   其他错误码透传自串口发送接口。
 * 调用方式：
 *   仅在采集模式下调用，上位机会把输出逐行保存成训练数据。
 */
fsp_err_t GestureCaptureService_ProcessFrame (mid_uart_port_t tx_port, glove_frame_t const * p_frame)
{
    fsp_err_t err = FSP_SUCCESS;

    if (NULL == p_frame)
    {
        return FSP_ERR_ASSERTION;
    }

    /*
     * 采集链路只导出协议层已经认定为有效的数据。
     * 这样可以避免 CRC 错包、无效帧和占位帧混入训练集。
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

    return FSP_SUCCESS;
}
