#ifndef DEBUG_UART_GLOVE_FRAME_H_
#define DEBUG_UART_GLOVE_FRAME_H_

#include "mid_uart.h"
#include <stdint.h>

/*
 * STM32 -> RA6M5 固定长度特征帧协议定义。
 * 这一层只负责“协议帧长什么样、怎么校验、怎么转成结构体”，
 * 不直接操作底层 g_uart2/g_uart7，也不掺杂手势识别业务。
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
 * 这样两端看到的 status 含义完全一致，后面增加联动功能时不会重复翻译。
 */
#define GLOVE_FRAME_STATUS_DATA_VALID     (1U << 0)
#define GLOVE_FRAME_STATUS_KEY1_PRESSED   (1U << 1)
#define GLOVE_FRAME_STATUS_KEY2_PRESSED   (1U << 2)
#define GLOVE_FRAME_STATUS_MODE_START     (1U << 3)
#define GLOVE_FRAME_STATUS_MODE1          (1U << 4)
#define GLOVE_FRAME_STATUS_MODE2          (1U << 5)
#define GLOVE_FRAME_STATUS_MODE3          (1U << 6)

/*
 * 解析完成后的协议对象。
 * feature_raw[] 保留串口原始定点值，便于你后面做原样存档、回放或对比；
 * feature[] 则是已经还原成 float 的结果，便于后续分类模块直接调用。
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
 * 计算 CRC16-CCITT-FALSE。
 * STM32 发包端与 RA 接收端都使用这一套算法，确保校验一致。
 */
uint16_t GloveFrame_Crc16(uint8_t const * p_data, uint16_t length);

/*
 * 解析一整帧 31 字节数据：
 * 1. 校验帧头；
 * 2. 校验版本号；
 * 3. 校验 CRC；
 * 4. 把小端 int16 特征还原成 float。
 * 这个接口不负责从串口收字节，只负责“解包”。
 */
fsp_err_t GloveFrame_ParsePacket(uint8_t const * p_packet, glove_frame_t * p_frame);

/*
 * 从指定串口持续接收数据，自动寻找 AA 55 帧头，并在收到合法完整帧后返回。
 * 这里默认使用阻塞式接收，适合当前裸机调试阶段。
 */
fsp_err_t GloveFrame_Receive(mid_uart_port_t rx_port, glove_frame_t * p_frame);

/*
 * 通过调试串口打印一帧人类可读的信息，便于串口助手观察：
 * 1. 帧序号、时间戳、状态字、CRC；
 * 2. 10 维特征值；
 * 3. 状态字对应的标志位名称。
 */
fsp_err_t GloveFrame_PrintDebug(mid_uart_port_t tx_port, glove_frame_t const * p_frame);

#endif /* DEBUG_UART_GLOVE_FRAME_H_ */
