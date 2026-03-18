#ifndef DEBUG_UART_GLOVE_FRAME_H_
#define DEBUG_UART_GLOVE_FRAME_H_

#include "mid_uart.h"
#include <stdint.h>

/*
 * 模块说明：
 * 1. 本模块属于协议解析中间层，负责把 STM32 发来的固定长度二进制帧还原成结构体。
 * 2. 它只关心“帧格式、帧头、CRC、字段还原”，不掺杂手势识别业务。
 * 3. 对外只暴露“收一帧并解析成功”的接口，CRC 和解包细节都留在 .c 内部。
 */
#define GLOVE_FRAME_SOF1                  0xAAU
#define GLOVE_FRAME_SOF2                  0x55U
#define GLOVE_FRAME_VERSION               0x01U
#define GLOVE_FRAME_FEATURE_COUNT         10U
#define GLOVE_FRAME_FEATURE_SCALE         100.0f
#define GLOVE_FRAME_PAYLOAD_WITHOUT_CRC   29U
#define GLOVE_FRAME_TOTAL_SIZE            31U

/*
 * 状态字位定义，保持与 STM32 端 trans.h 一致。
 * 上层读到这些位后，不需要再重新翻译协议语义。
 */
#define GLOVE_FRAME_STATUS_DATA_VALID     (1U << 0)
#define GLOVE_FRAME_STATUS_KEY1_PRESSED   (1U << 1)
#define GLOVE_FRAME_STATUS_KEY2_PRESSED   (1U << 2)
#define GLOVE_FRAME_STATUS_MODE_START     (1U << 3)
#define GLOVE_FRAME_STATUS_MODE1          (1U << 4)
#define GLOVE_FRAME_STATUS_MODE2          (1U << 5)
#define GLOVE_FRAME_STATUS_MODE3          (1U << 6)

/*
 * 结构体作用：
 *   保存一帧协议数据解析完成后的全部字段。
 * 字段说明：
 *   ver: 协议版本号。
 *   seq: 帧序号。
 *   tick_ms: STM32 侧打包时的毫秒计时。
 *   feature_raw: 原始 int16 定点特征，便于原样保存和问题回放。
 *   feature: 还原后的浮点特征，供识别和采集导出直接使用。
 *   status: 状态字。
 *   crc16: 原帧里携带的 CRC 校验值。
 */
typedef struct st_glove_frame
{
    uint8_t  ver;
    uint8_t  seq;
    uint32_t tick_ms;
    int16_t  feature_raw[GLOVE_FRAME_FEATURE_COUNT];
    float    feature[GLOVE_FRAME_FEATURE_COUNT];
    uint8_t  status;
    uint16_t crc16;
} glove_frame_t;

/*
 * 函数作用：
 *   从指定串口持续接收数据，自动完成帧头同步、CRC 校验和字段解析。
 * 调用时机：
 *   应用层主循环每次需要处理一帧新数据时调用。
 * 参数说明：
 *   rx_port: 协议帧来源串口，当前工程固定为 MID_UART_PORT_2。
 *   p_frame: 输出的帧结构体地址，函数成功后会写入解析结果。
 * 返回值：
 *   FSP_SUCCESS 表示已经成功拿到一帧合法数据；
 *   FSP_ERR_ASSERTION 表示输出指针为空；
 *   其他错误码表示底层收串口失败。
 * 调用方式：
 *   本函数是协议层对外唯一入口。调用返回成功后，上层就可以直接使用 p_frame 中的字段。
 */
fsp_err_t GloveFrame_Receive(mid_uart_port_t rx_port, glove_frame_t * p_frame);

#endif /* DEBUG_UART_GLOVE_FRAME_H_ */
