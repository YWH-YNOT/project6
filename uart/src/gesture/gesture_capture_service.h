#ifndef GESTURE_GESTURE_CAPTURE_SERVICE_H_
#define GESTURE_GESTURE_CAPTURE_SERVICE_H_

#include "debug_uart/glove_frame.h"

/*
 * 手势采集导出业务层
 * 1. 输入仍然是已经解包后的 glove_frame_t；
 * 2. 业务层只负责把有效特征导出成上位机可直接保存的 CSV 文本；
 * 3. 这样上位机工具只管打标签和落盘，不需要理解板端二进制协议。
 */
void GestureCaptureService_Init(void);
fsp_err_t GestureCaptureService_ProcessFrame(mid_uart_port_t tx_port, glove_frame_t const * p_frame);

#endif /* GESTURE_GESTURE_CAPTURE_SERVICE_H_ */
