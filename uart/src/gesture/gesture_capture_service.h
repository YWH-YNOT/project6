#ifndef GESTURE_GESTURE_CAPTURE_SERVICE_H_
#define GESTURE_GESTURE_CAPTURE_SERVICE_H_

#include "debug_uart/glove_frame.h"

/*
 * 模块说明：
 * 1. 本模块属于采集导出业务层；
 * 2. 输入仍然是已经解包完成的 glove_frame_t；
 * 3. 输出则是上位机可直接保存的 CSV 文本；
 * 4. 模块本身不维护标签和文件，只负责把板端数据变成干净的文本行。
 */

/*
 * 函数作用：
 *   把一帧有效特征导出成一行 CSV 文本并发到指定串口。
 * 调用时机：
 *   仅在采集模式下，由应用层在收到新帧后调用。
 * 参数说明：
 *   tx_port: CSV 文本输出串口，当前工程固定为 MID_UART_PORT_7。
 *   p_frame: 当前收到的协议帧。
 * 返回值：
 *   FSP_SUCCESS 表示导出完成；
 *   FSP_ERR_ASSERTION 表示帧指针为空；
 *   其他错误码透传自串口发送接口。
 * 调用方式：
 *   函数内部会自动过滤无效帧；只有 DATA_VALID 的帧才会真正导出。
 */
fsp_err_t GestureCaptureService_ProcessFrame(mid_uart_port_t tx_port, glove_frame_t const * p_frame);

#endif /* GESTURE_GESTURE_CAPTURE_SERVICE_H_ */
