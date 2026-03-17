#ifndef GESTURE_GESTURE_SERVICE_H_
#define GESTURE_GESTURE_SERVICE_H_

#include "debug_uart/glove_frame.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * 业务层节拍定义：
 * 1. UART 接收是持续进行的；
 * 2. 每 100ms 从最近合法帧里抽取一次特征样本；
 * 3. 每 200ms 由定时器标志驱动一次 SVM 分类与命令派发判定。
 */
#define GESTURE_SERVICE_FEATURE_COUNT       GLOVE_FRAME_FEATURE_COUNT
#define GESTURE_SERVICE_SAMPLE_PERIOD_MS    100U
#define GESTURE_SERVICE_CLASSIFY_PERIOD_MS  200U
#define GESTURE_SERVICE_SEND_CONFIRM_COUNT  2U

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
 * 3. send_ready 表示这次分类结果已经通过过滤，可以真正发送命令；
 * 4. suppress_common/send_hold 分别用于标识 0x02 屏蔽和“等待二次确认”。
 */
typedef struct st_gesture_service_result
{
    bool     sample_updated;
    bool     classify_ready;
    bool     send_ready;
    bool     suppress_common;
    bool     send_hold;
    uint8_t  label;
    uint8_t  cmd;
    uint8_t  send_cmd;
    uint8_t  history_cmd;
    uint8_t  confirm_count;
    uint8_t  sample_seq;
    uint8_t  sample_status;
    uint32_t sample_tick_ms;
    uint32_t sample_count;
    uint32_t classify_count;
    int16_t  feature_raw[GESTURE_SERVICE_FEATURE_COUNT];
    float    feature[GESTURE_SERVICE_FEATURE_COUNT];
} gesture_service_result_t;

/* 初始化业务层上下文，包括样本缓存和命令过滤状态。 */
void GestureService_Init(void);
/* 持续喂入 uart2 收到的合法协议帧，业务层内部按 100ms 维护最新样本。 */
fsp_err_t GestureService_ProcessFrame(glove_frame_t const * p_frame, gesture_service_result_t * p_result);
/* 在 200ms 定时标志到来时调用，执行一次分类和命令派发判定。 */
fsp_err_t GestureService_RunClassifyCycle(gesture_service_result_t * p_result);
/* 通过 uart7 输出分类结果、过滤状态和最终派发结论。 */
fsp_err_t GestureService_PrintResult(mid_uart_port_t tx_port, gesture_service_result_t const * p_result);

#endif /* GESTURE_GESTURE_SERVICE_H_ */
