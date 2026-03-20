#ifndef GESTURE_GESTURE_SERVICE_H_
#define GESTURE_GESTURE_SERVICE_H_

#include "debug_uart/glove_frame.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * 模块说明：
 * 1. 本模块属于手势识别业务层，是“协议帧 -> 采样缓存 -> SVM 分类 -> 控制命令”的核心。
 * 2. UART 接收是持续进行的，但业务层不会对每一帧都立刻分类；
 * 3. 它会按 50ms 节拍更新一次“最新样本”，再按 100ms 节拍执行一次 SVM 分类和命令过滤。
 * 4. “手势三”不再走规则旁路，而是作为 SVM 的一个独立标签参与正常分类流程。
 */
#define GESTURE_SERVICE_FEATURE_COUNT       GLOVE_FRAME_FEATURE_COUNT
#define GESTURE_SERVICE_SAMPLE_PERIOD_MS    50U
#define GESTURE_SERVICE_CLASSIFY_PERIOD_MS  100U
#define GESTURE_SERVICE_SEND_CONFIRM_COUNT  2U

/*
 * 命令定义说明：
 * 这些命令值保持与原 STM32 侧 control.c 的命令映射一致，
 * 这样迁移后的 RA 工程在对外行为上与原工程保持兼容。
 */
#define GESTURE_CMD_NONE                    0x00U
#define GESTURE_CMD_COMMON                  0x02U
#define GESTURE_CMD_ONE                     0x69U
#define GESTURE_CMD_ROCK                    0x05U
#define GESTURE_CMD_THREE                   0x19U
#define GESTURE_CMD_UP                      0x08U
#define GESTURE_CMD_DOWN                    0x0AU
#define GESTURE_CMD_LEFT                    0x06U
#define GESTURE_CMD_RIGHT                   0x07U

/*
 * 结构体作用：
 *   保存一次业务处理输出的可观测结果，便于应用层做发送控制和调试。
 * 字段说明：
 *   sample_updated: 这次输入是否刷新了 50ms 样本缓存。
 *   classify_ready: 这次是否真的执行了一次 100ms 分类。
 *   send_ready: 这次分类结果是否已经通过发送过滤，可以被应用层锁存为当前激活发送命令。
 *   suppress_common: 这次结果是否因为命令值为 0x02 而被压制。
 *   send_hold: 这次结果是否还处于“等待二次确认”的暂存阶段。
 *   label: SVM 输出的类别标签。
 *   cmd: 标签映射后的控制命令。
 *   send_cmd: 真正允许对外发送的命令值。
 *   history_cmd: 过滤器当前记录的历史命令。
 *   confirm_count: 当前历史命令已累计确认次数。
 *   sample_seq/sample_status/sample_tick_ms: 当前样本来自哪一帧。
 *   sample_count: 自初始化以来成功采样的样本数。
 *   classify_count: 自初始化以来真正执行的分类次数。
 *   feature_raw/feature: 当前参与分类的样本特征。
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

/*
 * 函数作用：
 *   初始化手势识别业务层的全部内部状态。
 * 调用时机：
 *   系统启动时调用一次；每次运行模式切换回识别链路时也会重新调用。
 * 参数说明：
 *   无。
 * 返回值：
 *   无。
 * 调用方式：
 *   调用后会清空样本缓存、历史命令和确认计数，相当于把识别流程复位到初始状态。
 */
void GestureService_Init(void);

/*
 * 函数作用：
 *   持续喂入一帧已经解包完成的手套数据，并在需要时更新 50ms 样本缓存。
 * 调用时机：
 *   应用层每收到一帧合法协议数据后调用一次。
 * 参数说明：
 *   p_frame: 当前收到的合法协议帧。
 *   p_result: 输出结果结构体，函数会写入本次处理结果。
 * 返回值：
 *   FSP_SUCCESS 表示处理完成；
 *   FSP_ERR_ASSERTION 表示输入参数为空。
 * 调用方式：
 *   这是识别链路的第一步。它只负责“选样本”，不负责真正执行 SVM 分类。
 */
fsp_err_t GestureService_ProcessFrame(glove_frame_t const * p_frame, gesture_service_result_t * p_result);

/*
 * 函数作用：
 *   在 100ms 分类节拍到来时，使用最新样本执行一次 SVM 分类和命令过滤。
 * 调用时机：
 *   只有当应用层确认已经到了一个新的 100ms 分类周期时才调用。
 * 参数说明：
 *   p_result: 输出结果结构体，函数会写入分类和过滤结果。
 * 返回值：
 *   FSP_SUCCESS 表示本次周期处理完成；
 *   FSP_ERR_ASSERTION 表示结果指针为空。
 * 调用方式：
 *   如果返回后 p_result->send_ready 为 true，应用层应该先把 p_result->send_cmd 更新成当前激活发送命令，
 *   再在后续每个独立的 200ms 发送窗口里持续派发，直到命令被新结果替换或被稳定释放。
 */
fsp_err_t GestureService_RunClassifyCycle(gesture_service_result_t * p_result);

#endif /* GESTURE_GESTURE_SERVICE_H_ */
