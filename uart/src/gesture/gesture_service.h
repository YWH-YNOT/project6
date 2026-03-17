#ifndef GESTURE_GESTURE_SERVICE_H_
#define GESTURE_GESTURE_SERVICE_H_

#include "debug_uart/glove_frame.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * 业务层节拍定义：
 * 1. UART 接收是持续进行的；
 * 2. 每 100ms 从最近合法帧里抽取一次特征样本；
 * 3. 每 200ms 做一次 SVM 分类，并得到一条待发送命令。
 */
#define GESTURE_SERVICE_FEATURE_COUNT       GLOVE_FRAME_FEATURE_COUNT
#define GESTURE_SERVICE_SAMPLE_PERIOD_MS    100U
#define GESTURE_SERVICE_CLASSIFY_PERIOD_MS  200U

/* 与 STM32 control.c 当前命令映射保持一致。 */
#define GESTURE_CMD_NONE                    0x00U
#define GESTURE_CMD_COMMON                  0x02U
#define GESTURE_CMD_ONE                     0x69U
#define GESTURE_CMD_ROCK                    0x05U
#define GESTURE_CMD_UP                      0x08U
#define GESTURE_CMD_DOWN                    0x0AU
#define GESTURE_CMD_LEFT                    0x06U
#define GESTURE_CMD_RIGHT                   0x07U

/*
 * 业务层输出结果：
 * 1. sample_updated 表示这次帧输入让 100ms 样本缓存更新了；
 * 2. classify_ready 表示这次已经完成一次 200ms 分类；
 * 3. 只有 classify_ready 为 true 时，label/cmd 才表示一条新结果。
 */
typedef struct st_gesture_service_result
{
    bool     sample_updated;
    bool     classify_ready;
    uint8_t  label;
    uint8_t  cmd;
    uint8_t  sample_seq;
    uint8_t  sample_status;
    uint32_t sample_tick_ms;
    uint32_t sample_count;
    uint32_t classify_count;
    int16_t  feature_raw[GESTURE_SERVICE_FEATURE_COUNT];
    float    feature[GESTURE_SERVICE_FEATURE_COUNT];
} gesture_service_result_t;

void GestureService_Init(void);
fsp_err_t GestureService_ProcessFrame(glove_frame_t const * p_frame, gesture_service_result_t * p_result);
fsp_err_t GestureService_PrintResult(mid_uart_port_t tx_port, gesture_service_result_t const * p_result);

#endif /* GESTURE_GESTURE_SERVICE_H_ */
