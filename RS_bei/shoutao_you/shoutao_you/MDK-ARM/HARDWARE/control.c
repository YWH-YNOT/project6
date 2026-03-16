#include "control.h"
#include "gesture_model.h" // 9类统一手势（fist/open/one/two/rock/up/down/left/right）
#include <math.h>
#include <stdio.h>

/* 挥手SVM模型头文件（由 train_swipe_svm.py 生成后手动复制到此目录）
 * 首次编译前请先完成数据采集和训练，然后复制 swipe_model.h 到 HARDWARE/
 * 若文件尚未生成，注释掉下面这行并替换为空实现即可先编译通过 */
#include "swipe_model.h"

// 模式标志（main.c 引用）
int mode_start = 0;
int mode1_flag = 0;
int mode2_flag = 0;
int mode3_flag = 0;

// ============================================================================
// 挥手方向识别模块 —— SVM 版本
//
// 流程：
//   1. 每帧将 (roll, pitch, gyro_z, gyro_y) 压入循环缓冲区
//   2. 用峰值-回归检测器判断"一次挥手"是否完成
//   3. 挥手完成时：从缓冲区提取 24 维统计特征
//   4. 调用 swipe_model.h 中的 Swipe_Classify() 推理方向
//   5. 将结果映射到 CMD_LEFT / CMD_RIGHT / CMD_UP / CMD_DOWN
//
// 特征定义（与 PC端 train_swipe_svm.py 完全一致）：
//   每通道 6 个统计量：max, min, range, mean, delta, peak_pos
//   通道：roll(0) pitch(1) gyro_z(2) gyro_y(3)  → 共 24 维
//
// 可调参数（根据实际效果调整）：
//   SWIPE_DEPART_THRESH  — 出发阈值（角度偏离多少才开始追踪）
//   SWIPE_RETURN_THRESH  — 回归阈值（回到多近才算完成）
//   SWIPE_PEAK_MIN       — 峰值下限（防止小抖动触发）
// ============================================================================

#define SW_WINDOW 30 // 循环缓冲区帧数（需与 WINDOW_SIZE 一致）
#define SW_N_CH 4    // 采集通道数：roll, pitch, gyro_z, gyro_y
#define SW_N_FEAT 24 // 特征维数（= SW_N_CH × 6）

// ---- 峰值回归检测器阈值 ----
#define SWIPE_DEPART_THRESH 25.0f  // 偏离基线 >25° 开始追踪
#define SWIPE_RETURN_THRESH 10.0f  // 回归到 <10° 时确认挥手完成
#define SWIPE_PEAK_MIN 25.0f       // 峰值至少 25°，防止抖动触发
#define SWIPE_BASELINE_ALPHA 0.05f // 基线低通系数
#define SWIPE_COOLDOWN_FRAMES 60   // 冷却帧数（~600ms @ 100Hz）

// ---- 循环缓冲区 ----
static float sw_buf[SW_WINDOW][SW_N_CH]; // [frame_idx][channel]
static uint8_t sw_head = 0;              // 下一帧写入位置
static uint8_t sw_count = 0;             // 已有帧数（<= SW_WINDOW）

// ---- 峰值回归状态机 ----
typedef enum { SWIPE_IDLE = 0, SWIPE_TRACKING, SWIPE_COOLDOWN } SwipeState_t;
static SwipeState_t sw_state = SWIPE_IDLE;
static float sw_base_roll = 0.0f;
static float sw_base_pitch = 0.0f;
static float sw_peak_delta = 0.0f; // 最大偏离（含符号，roll或pitch）
static uint8_t sw_peak_axis = 0;   // 0=roll, 1=pitch
static uint32_t sw_cd_tick = 0;
static uint8_t sw_init = 0;

// ============================================================================
// 辅助函数：提取单通道统计特征（6维）
// ============================================================================
static void extract_channel_feats(uint8_t ch, float *out6) {
  // 从循环缓冲区（sw_buf, sw_head, sw_count）读出该通道的时序
  float x_max = -1e30f, x_min = 1e30f, x_sum = 0.0f;
  float x_first, x_last;
  float peak_abs = 0.0f;
  uint8_t peak_idx = 0;
  uint8_t n = (sw_count < SW_WINDOW) ? sw_count : SW_WINDOW;

  for (uint8_t k = 0; k < n; k++) {
    // 按时间顺序（最旧→最新）读取
    uint8_t idx = (uint8_t)((sw_head + SW_WINDOW - n + k) % SW_WINDOW);
    float v = sw_buf[idx][ch];
    if (v > x_max)
      x_max = v;
    if (v < x_min)
      x_min = v;
    x_sum += v;
    float av = v > 0 ? v : -v;
    if (av > peak_abs) {
      peak_abs = av;
      peak_idx = k;
    }
    if (k == 0)
      x_first = v;
    x_last = v;
  }

  out6[0] = x_max;
  out6[1] = x_min;
  out6[2] = x_max - x_min;                                       // range
  out6[3] = x_sum / (float)n;                                    // mean
  out6[4] = x_last - x_first;                                    // delta
  out6[5] = (n > 1) ? ((float)peak_idx / (float)(n - 1)) : 0.0f; // peak_pos
}

// ============================================================================
// 主函数：每帧调用，维护缓冲区 + 检测挥手 + SVM分类
// ============================================================================
/**
 * @brief  挥手方向识别（SVM版），在主循环中每帧调用
 *         识别成功后设置 current_cmd，由 main.c 的 trans_flag 逻辑发送
 */
void Gesture_Move(void) {
  // ---- 读取当前帧 (roll, pitch, gyro_z, gyro_y) 压入循环缓冲区 ----
  float roll = jy901s_data.angle_roll;
  float pitch = jy901s_data.angle_pitch;
  float gyro_z = jy901s_data.gyro_z;
  float gyro_y = jy901s_data.gyro_y;

  sw_buf[sw_head][0] = roll;
  sw_buf[sw_head][1] = pitch;
  sw_buf[sw_head][2] = gyro_z;
  sw_buf[sw_head][3] = gyro_y;
  sw_head = (sw_head + 1) % SW_WINDOW;
  if (sw_count < SW_WINDOW)
    sw_count++;

  // ---- 基线初始化 ----
  if (!sw_init) {
    sw_base_roll = roll;
    sw_base_pitch = pitch;
    sw_init = 1;
    return;
  }

  float dr = roll - sw_base_roll;
  float dp = pitch - sw_base_pitch;
  float abs_dr = dr > 0 ? dr : -dr;
  float abs_dp = dp > 0 ? dp : -dp;

  // ============= 峰值回归状态机 =============
  switch (sw_state) {
  // ---- IDLE：基线缓慢跟随，检测出发 ----
  case SWIPE_IDLE: {
    sw_base_roll += SWIPE_BASELINE_ALPHA * dr;
    sw_base_pitch += SWIPE_BASELINE_ALPHA * dp;

    if (abs_dr >= SWIPE_DEPART_THRESH && abs_dr >= abs_dp) {
      sw_peak_delta = dr;
      sw_peak_axis = 0;
      sw_state = SWIPE_TRACKING;
    } else if (abs_dp >= SWIPE_DEPART_THRESH && abs_dp > abs_dr) {
      sw_peak_delta = dp;
      sw_peak_axis = 1;
      sw_state = SWIPE_TRACKING;
    }
    break;
  }

  // ---- TRACKING：追踪峰值，等待回归 ----
  case SWIPE_TRACKING: {
    float cur = (sw_peak_axis == 0) ? dr : dp;
    float ac = cur > 0 ? cur : -cur;
    float apk = sw_peak_delta > 0 ? sw_peak_delta : -sw_peak_delta;

    // 同向且更大→更新峰值
    if (((cur > 0) == (sw_peak_delta > 0)) && ac > apk)
      sw_peak_delta = cur;

    apk = sw_peak_delta > 0 ? sw_peak_delta : -sw_peak_delta;

    // 回归且峰值足够大 → SVM分类
    if (ac <= SWIPE_RETURN_THRESH && apk >= SWIPE_PEAK_MIN) {
      if (sw_count >= SW_WINDOW) // 缓冲区已满才推理
      {
        // 提取 24 维特征
        float feat[SW_N_FEAT];
        for (uint8_t ch = 0; ch < SW_N_CH; ch++)
          extract_channel_feats(ch, &feat[ch * 6]);

        uint8_t dir = Swipe_Classify(feat);

        // 映射到命令
        switch (dir) {
        case 0:
          current_cmd = 0x06;
          break;
        case 1:
          current_cmd = 0x07;
          break;
        case 2:
          current_cmd = 0x08;
          break;
        case 3:
          current_cmd = 0x0A;
          break;
        default:
          current_cmd = CMD_NONE;
          break;
        }

        if (dir < 4) {
          printf("[挥手 SVM] %s  (peak=%.1f)\r\n", sm_labels[dir], apk);
        }
      } else {
        // 缓冲区不足，用简单方向判断兜底
        current_cmd = (sw_peak_axis == 0)
                          ? (sw_peak_delta > 0 ? 0x07 : 0x06)
                          : (sw_peak_delta > 0 ? 0x0A : 0x08);
      }

      sw_cd_tick = 0;
      sw_state = SWIPE_COOLDOWN;
      sw_base_roll = roll;
      sw_base_pitch = pitch;
    }
    // 回归但峰值太小 → 取消
    else if (ac <= SWIPE_RETURN_THRESH) {
      sw_base_roll = roll;
      sw_base_pitch = pitch;
      sw_state = SWIPE_IDLE;
    }
    break;
  }

  // ---- COOLDOWN：冷却期 ----
  case SWIPE_COOLDOWN: {
    sw_cd_tick++;
    if (sw_cd_tick >= SWIPE_COOLDOWN_FRAMES) {
      current_cmd = CMD_NONE;
      sw_state = SWIPE_IDLE;
    }
    break;
  }

  default:
    sw_state = SWIPE_IDLE;
    break;
  }
}

// ============================================================================
// 统一手势识别模块（v5 · 9类 · 定时轮询）
//
// 每 GR_PERIOD 帧（0.2s @ 100Hz）采样一次：
//   label 5-8（方向手势）→  current_cmd = CMD_UP/DOWN/LEFT/RIGHT  + 打印
//   其余（原有手势/无效）→  current_cmd = 0x00（NONE）            + 打印 NONE
// ============================================================================

#define GR_PERIOD 5 // 采样周期帧数（100Hz × 0.2s）

static const char *gr_label_names[9] = {
    "fist", "open", "one", "two", "rock", "up", "down", "left", "right",
};

static uint32_t gr_tick = 0;

void Gesture_Run(void) {
  if (++gr_tick < GR_PERIOD)
    return;
  gr_tick = 0;

  // ---- SVM 推理 ----
  uint8_t label =
      Gesture_Classify(mpu_data[0].filter_angle_x, mpu_data[0].filter_angle_y,
                       mpu_data[1].filter_angle_x, mpu_data[1].filter_angle_y,
                       mpu_data[2].filter_angle_x, mpu_data[2].filter_angle_y,
                       mpu_data[3].filter_angle_x, mpu_data[3].filter_angle_y,
                       jy901s_data.angle_roll, jy901s_data.angle_pitch);

  // ---- 命令映射表（9类手势 → 各自命令字节，分类失败才发 NONE）----
  static const uint8_t gr_cmd_map[9] = {
      0x02,      // 0: fist  握拳
      0x02,      // 1: open  张开
      0x69,      // 2: one   比1
      0x02,      // 3: two   比2/V
      0x05,      // 4: rock  摇滚
      0x08,      // 5: up    ↑
      0x0A,      // 6: down  ↓
      0x06,      // 7: left  ←
      0x07,      // 8: right →
  };

  if (label < GM_N_CLASSES) {
    current_cmd = gr_cmd_map[label];
//		if(current_cmd != 0x02)
//		{
//			printf("[手势] %d - %s  (0x%02X)\r\n", label, gr_label_names[label],
//						 current_cmd);
//		}
  } else {
    current_cmd = 0x00; // NONE：分类失败
    printf("[手势] NONE\r\n");
  }

//  // ---- 每 0.2s 通过 huart6 发送一次 current_cmd（1字节）----
//  HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xFFFF);
}

void Gesture_Test(void) { Gesture_Run(); } // 向后兼容别名

void Gesture_Control_pro(void) { /* 预留 */ }
