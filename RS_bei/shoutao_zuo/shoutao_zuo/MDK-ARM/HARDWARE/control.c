#include "control.h"
#include "headfile.h"
int mode_start = 0;
int mode1_flag = 0;
int mode2_flag = 0;
int mode3_flag = 0;
/*
层级1：判断是否握拳
层级2：判断判断加速度大小，是否开始运动

手指比1，key1，按键视觉     key2，按键动作
手指比2，key1，开关视觉     key2，开关动作



*/

///#############################################################################################

//判断手势（精细）
// 定义手状态枚举
typedef enum {
    HAND_UNKNOWN,   // 未知状态
    HAND_OPEN,      // 张开状态
    HAND_CLOSED     // 握拳状态
		
} HandState;


HandState current_hand_state = HAND_UNKNOWN;  // 当前手状态
uint8_t open_cnt = 0;  // 张开状态计数（防抖）
uint8_t closed_cnt = 0;  // 握拳状态计数（防抖）
#define STABLE_CNT_THRESH 3  // 稳定状态所需帧数（防抖阈值）

/**
 * @brief  判断手的状态：张开或握拳
 * @note   基于手指（0-3通道）与手背（4通道）的角度差进行判断
 */
void Gesture_Control_pro(void) 
{
    float finger_angles[4] = {0};  // 存储4根手指的滤波角度（filter_angle_x）
    float back_angle = 0;          // 手背的滤波角度（filter_angle_x）
    float angle_diff_avg = 0;      // 手指与手背的平均角度差

    // 读取4根手指和手背的角度（通道0-3为手指，roll为手背）
    for (int i = 0; i < 4; i++) 
		{
        finger_angles[i] = mpu_data[i].filter_angle_x;
    }
    back_angle = jy901s_data.angle_roll;

    // 计算手指与手背的平均角度差
    for (int i = 0; i < 4; i++) 
		{
        angle_diff_avg += fabs(finger_angles[i] - back_angle);
    }
    angle_diff_avg /= 4;
		
		if(mpu_data[0].filter_angle_x> 30 && mpu_data[1].filter_angle_x <0 && mpu_data[2].filter_angle_x<0 && mpu_data[3].filter_angle_x<0)
		{	
			mode_start = 1;	
			mode1_flag = 1;				//开启模式1
			mode2_flag = 0;				//清零模式2
			mode3_flag = 0;
			allow_send_flag = 3;
		}
		else if(mpu_data[0].filter_angle_x> 30 && mpu_data[1].filter_angle_x >30 && mpu_data[2].filter_angle_x<0 && mpu_data[3].filter_angle_x<0)
		{	
			mode_start = 1;
			mode2_flag = 1;				//开启模式2
			mode1_flag = 0;				//清零模式1
			mode3_flag = 0;
			allow_send_flag = 3;
		}
		else if(mpu_data[3].filter_angle_x> 30 && mpu_data[2].filter_angle_x < 0 && mpu_data[1].filter_angle_x<0 && mpu_data[0].filter_angle_x>30)
		{	
			mode_start = 1;
			mode3_flag = 1;				//开启模式3
			mode1_flag = 0;				//清零模式1
			mode2_flag = 0;
			allow_send_flag = 3;
		}
		else
		{
			mode_start = 0;
			mode1_flag = 0;
			mode2_flag = 0;				//全部清零
			mode3_flag = 0;
			// 状态判断与防抖
			if (angle_diff_avg < 40) 
			{  // 张开时角度差大（需根据实际数据调整阈值）
					open_cnt++;
					closed_cnt = 0;
					if (open_cnt >= STABLE_CNT_THRESH && current_hand_state != HAND_OPEN) 
					{
							current_hand_state = HAND_OPEN;						
	//            printf("手状态：张开\r\n");
							allow_send_flag = 1;
						 
					}
			} 
			else if(angle_diff_avg > 40)
			{  // 握拳时角度差小
					closed_cnt++;
					open_cnt = 0;
					if (closed_cnt >= STABLE_CNT_THRESH && current_hand_state != HAND_CLOSED) 
					{
							current_hand_state = HAND_CLOSED;
	//            printf("手状态：握拳\r\n");
							allow_send_flag = 0;
						
					}
			}
		}

    // 调试打印（可选）
//     printf("平均角度差：%.2f, 当前状态：%d\r\n", angle_diff_avg, current_hand_state);
}

// ============================================================================
// 手势运动识别模块（从mpu6050.c移至此以提高可移植性）
// ============================================================================

#include "JY901S.h"
#include "mpu6050.h"
#include <stdio.h>
#include <math.h>

// 运动方向枚举
typedef enum {
    DIR_NONE,       // 无方向
    DIR_UP,         // 上
    DIR_DOWN,       // 下
    DIR_LEFT,       // 左
    DIR_RIGHT,      // 右
    DIR_UP_LEFT,    // 左上
    DIR_UP_RIGHT,   // 右上
    DIR_DOWN_LEFT,  // 左下
    DIR_DOWN_RIGHT  // 右下
} MoveDirection;

// 配置参数
#define FILTER_WINDOW_SIZE 5       // 滤波窗口大小
#define DIR_STABLE_CNT 15           // 防抖帧数
#define MOVE_VALID_THRESH 1.5f     // 有效运动总加速度阈值（过滤微小抖动）
#define VELOCITY_THRESH 1.5f       // 有效速度阈值（需更大运动才判定方向）
#define INTEGRAL_DRIFT_THRESH 0.7f // 积分死区（过滤抖动加速度）
#define VELOCITY_MAX 10.0f         // 速度上限（防止抖动累积溢出）
#define INIT_STABLE_DURATION 500   // 启动稳定期（500ms，等待传感器稳定）
#define CALIB_SAMPLE_CNT 50        // 校准采样次数（50次取平均，抑制零漂）
#define HPF_ALPHA 0.8f             // 高通滤波系数（保持去重力效果）
#define DECAY_SLOW 0.8f            // 慢衰减（防抖动：启动/方向未稳定时用）
#define DECAY_FAST 0.3f            // 快衰减（快响应：方向稳定后用）

// ============================================================================
// 手势运动识别模块 - 状态结构和辅助函数
// ============================================================================

// 手势运动状态结构体（封装所有状态变量，提高可移植性）
typedef struct {
    // 加速度历史数据
    float last_acc_x;          // 上一次X轴加速度
    float last_acc_y;          // 上一次Y轴加速度
    float last_acc_z;          // 上一次Z轴加速度
    
    // 滤波相关变量
    float delta_x_history[FILTER_WINDOW_SIZE];  // X轴差值历史
    float delta_y_history[FILTER_WINDOW_SIZE];  // Y轴差值历史
    float delta_z_history[FILTER_WINDOW_SIZE];  // Z轴差值历史
    uint8_t filter_index;      // 滤波窗口索引
    
    // 初始化和校准状态
    uint8_t is_initialized;    // 初始化标志（0：未初始化，1：已初始化）
    uint32_t init_stable_time; // 初始稳定期开始时间
    uint8_t init_stable_done;  // 0：未稳定，1：已稳定
    float acc_x_offset;        // X轴零漂校准基准
    float acc_y_offset;        // Y轴零漂校准基准
    uint8_t is_calibrated;     // 0：未校准，1：已校准
    uint8_t calib_cnt;         // 校准采样计数器
    
    // 速度和滤波状态
    float hpf_acc_x;           // 高通滤波后的X轴加速度
    float hpf_acc_y;           // 高通滤波后的Y轴加速度
    float vel_x;               // X轴速度
    float vel_y;               // Y轴速度
    
    // 方向状态
    MoveDirection current_dir; // 当前运动方向
    uint8_t dir_stable_cnt;    // 方向稳定计数器
} GestureMoveState;

// 内部辅助函数声明
static float weighted_filter_internal(float history[], float new_val, uint8_t window_size, uint8_t *filter_index);
static void GestureMove_Init(GestureMoveState *state);
static uint8_t GestureMove_HandleCalibration(GestureMoveState *state);
static void GestureMove_ProcessAcceleration(GestureMoveState *state, float acc_x, float acc_y, float acc_z);
static void GestureMove_FilterAcceleration(GestureMoveState *state, float *filt_acc_x, float *filt_acc_y, float *filt_acc_z);
static float GestureMove_SelectDecay(GestureMoveState *state);
static void GestureMove_CalculateVelocity(GestureMoveState *state, float filt_acc_x, float filt_acc_y, float decay);
static MoveDirection GestureMove_DetectDirection(GestureMoveState *state, float filt_acc_x, float filt_acc_y, float filt_acc_z, float *acc_num);
static void GestureMove_UpdateDirection(GestureMoveState *state, MoveDirection new_dir);
static void GestureMove_MapToCommand(GestureMoveState *state, float filt_acc_x, float filt_acc_y, float decay, float acc_num);

/**
 * @brief  加权平均滤波函数（内部使用）
 * @param  history: 历史数据数组
 * @param  new_val: 新值
 * @param  window_size: 窗口大小
 * @param  filter_index: 滤波索引（输入输出参数）
 * @retval 滤波后的值
 */
static float weighted_filter_internal(float history[], float new_val, uint8_t window_size, uint8_t *filter_index)
{
    // 存储新值到历史缓冲区
    history[*filter_index] = new_val;
    
    // 计算加权平均值（近期数据权重相等）
    float weights[FILTER_WINDOW_SIZE] = {0.2f, 0.2f, 0.2f, 0.2f, 0.2f};
    float sum = 0.0f;
    float weight_sum = 0.0f;
    
    for (uint8_t i = 0; i < window_size; i++) 
    {
        uint8_t idx = (*filter_index - i + window_size) % window_size;
        sum += history[idx] * weights[i];
        weight_sum += weights[i];
    }
    
    // 更新窗口索引
    *filter_index = (*filter_index + 1) % window_size;
    
    return sum / weight_sum;  // 归一化权重
}

/**
 * @brief  初始化手势运动识别模块
 * @param  state: 状态结构体指针
 * @note   初始化所有状态变量和滤波窗口
 */
static void GestureMove_Init(GestureMoveState *state)
{
    // 初始化加速度历史值
    state->last_acc_x = jy901s_data.acc_x;
    state->last_acc_y = jy901s_data.acc_y;
    state->last_acc_z = jy901s_data.acc_z;
      
    // 初始化滤波窗口
    for (uint8_t i = 0; i < FILTER_WINDOW_SIZE; i++) 
    {
        state->delta_x_history[i] = 0.0f;
        state->delta_y_history[i] = 0.0f;
        state->delta_z_history[i] = 0.0f;
    }

    // 初始化速度和滤波参数
    state->vel_x = 0.0f;
    state->vel_y = 0.0f;
    state->hpf_acc_x = 0.0f;
    state->hpf_acc_y = 0.0f;
    state->filter_index = 0;
    
    // 启动稳定期计时
    state->init_stable_time = HAL_GetTick();
    state->init_stable_done = 0;
    state->is_calibrated = 0;
    state->calib_cnt = 0;
    state->acc_x_offset = 0.0f;
    state->acc_y_offset = 0.0f;
    state->is_initialized = 1;
    state->current_dir = DIR_NONE;
    state->dir_stable_cnt = 0;
    
//    printf("8方向检测初始化完成，进入启动稳定期（%dms）...\r\n", INIT_STABLE_DURATION);
}

/**
 * @brief  处理启动稳定期和零漂校准
 * @param  state: 状态结构体指针
 * @retval 1：仍在稳定期，需要继续等待；0：稳定期结束，可以正常处理
 */
static uint8_t GestureMove_HandleCalibration(GestureMoveState *state)
{
    if (state->init_stable_done == 0) 
    {
        if (HAL_GetTick() - state->init_stable_time < INIT_STABLE_DURATION) 
        {
            // 稳定期内进行零漂校准（采样50次取平均）
            state->acc_x_offset += jy901s_data.acc_x;
            state->acc_y_offset += jy901s_data.acc_y;
            state->calib_cnt++;
            
            // 校准采样完成后计算平均值
            if (state->calib_cnt >= CALIB_SAMPLE_CNT) 
            {
                state->acc_x_offset /= CALIB_SAMPLE_CNT;
                state->acc_y_offset /= CALIB_SAMPLE_CNT;
                state->is_calibrated = 1;
//                printf("零漂校准完成：X轴基准=%.3f, Y轴基准=%.3f\r\n", 
//                       state->acc_x_offset, state->acc_y_offset);
            }

            // 更新历史加速度（让高通滤波适应）
            state->last_acc_x = jy901s_data.acc_x;
            state->last_acc_y = jy901s_data.acc_y;
            state->last_acc_z = jy901s_data.acc_z;
            
//            printf("启动稳定中...剩余时间：%dms\r\n", 
//                   INIT_STABLE_DURATION - (HAL_GetTick() - state->init_stable_time));
            return 1;  // 仍在稳定期
        }
        else 
        {
            // 稳定期结束：标记完成，重置状态
            state->init_stable_done = 1;
            state->vel_x = 0.0f;
            state->vel_y = 0.0f;
            state->dir_stable_cnt = 0;
            state->current_dir = DIR_NONE;
//            printf("启动稳定期结束，开始正常方向判断...\r\n");
        }
    }
    return 0;  // 稳定期结束
}

/**
 * @brief  处理加速度数据（读取、去零漂、高通滤波）
 * @param  state: 状态结构体指针
 * @param  acc_x: X轴原始加速度输入
 * @param  acc_y: Y轴原始加速度输入
 * @param  acc_z: Z轴原始加速度输入
 * @note   函数内部更新state中的滤波加速度和历史值
 */
static void GestureMove_ProcessAcceleration(GestureMoveState *state, float acc_x, float acc_y, float acc_z)
{
    // 读取原始加速度：减去零漂基准（抑制静态抖动）
    float raw_acc_x = acc_x - (state->is_calibrated ? state->acc_x_offset : 0.0f);
    float raw_acc_y = acc_y - (state->is_calibrated ? state->acc_y_offset : 0.0f);
    float raw_acc_z = acc_z;

    // 高通滤波：过滤重力和静态零漂
    state->hpf_acc_x = HPF_ALPHA * (state->hpf_acc_x + raw_acc_x - state->last_acc_x);
    state->hpf_acc_y = HPF_ALPHA * (state->hpf_acc_y + raw_acc_y - state->last_acc_y);

    // 更新历史加速度（用于下一次滤波）
    state->last_acc_x = raw_acc_x;
    state->last_acc_y = raw_acc_y;
    state->last_acc_z = raw_acc_z;
}

/**
 * @brief  对加速度进行加权滤波
 * @param  state: 状态结构体指针
 * @param  filt_acc_x: 输出滤波后的X轴加速度
 * @param  filt_acc_y: 输出滤波后的Y轴加速度
 * @param  filt_acc_z: 输出滤波后的Z轴加速度
 */
static void GestureMove_FilterAcceleration(GestureMoveState *state, float *filt_acc_x, float *filt_acc_y, float *filt_acc_z)
{
    *filt_acc_x = weighted_filter_internal(state->delta_x_history, state->hpf_acc_x, FILTER_WINDOW_SIZE, &state->filter_index);
    *filt_acc_y = weighted_filter_internal(state->delta_y_history, state->hpf_acc_y, FILTER_WINDOW_SIZE, &state->filter_index);
    *filt_acc_z = weighted_filter_internal(state->delta_z_history, state->last_acc_z, FILTER_WINDOW_SIZE, &state->filter_index);
}

/**
 * @brief  根据方向稳定性选择衰减系数
 * @param  state: 状态结构体指针
 * @retval 当前应使用的衰减系数
 */
static float GestureMove_SelectDecay(GestureMoveState *state)
{
    if (state->current_dir == DIR_NONE || state->dir_stable_cnt < DIR_STABLE_CNT) 
    {
        // 方向未稳定（无方向/正在防抖）：用慢衰减（防抖动）
        return DECAY_SLOW;
    }
    else 
    {
        // 方向已稳定：用快衰减（快速响应方向切换）
        return DECAY_FAST;
    }
}

/**
 * @brief  计算速度（积分逻辑：动态衰减+死区抑制）
 * @param  state: 状态结构体指针
 * @param  filt_acc_x: 滤波后的X轴加速度
 * @param  filt_acc_y: 滤波后的Y轴加速度
 * @param  decay: 衰减系数
 */
static void GestureMove_CalculateVelocity(GestureMoveState *state, float filt_acc_x, float filt_acc_y, float decay)
{
    // 先按当前衰减系数衰减速度（动态切换防抖/响应模式）
    state->vel_x *= decay;
    state->vel_y *= decay;

    // 积分死区：仅大于阈值的加速度才积分
    if (fabs(filt_acc_x) > INTEGRAL_DRIFT_THRESH) 
    {
        state->vel_x += filt_acc_x;
    }
    if (fabs(filt_acc_y) > INTEGRAL_DRIFT_THRESH) 
    {
        state->vel_y += filt_acc_y;
    }

    // 速度上限限制（防止异常抖动溢出）
    if (state->vel_x > VELOCITY_MAX) state->vel_x = VELOCITY_MAX;
    else if (state->vel_x < -VELOCITY_MAX) state->vel_x = -VELOCITY_MAX;
    if (state->vel_y > VELOCITY_MAX) state->vel_y = VELOCITY_MAX;
    else if (state->vel_y < -VELOCITY_MAX) state->vel_y = -VELOCITY_MAX;
}

/**
 * @brief  检测运动方向
 * @param  state: 状态结构体指针
 * @param  filt_acc_x: 滤波后的X轴加速度
 * @param  filt_acc_y: 滤波后的Y轴加速度
 * @param  filt_acc_z: 滤波后的Z轴加速度
 * @param  acc_num: 输出的总加速度（用于判断有效运动）
 * @retval 检测到的运动方向
 */
static MoveDirection GestureMove_DetectDirection(GestureMoveState *state, float filt_acc_x, float filt_acc_y, float filt_acc_z, float *acc_num)
{
    // 计算总加速度（判断有效运动）
    *acc_num = sqrt(filt_acc_x * filt_acc_x + filt_acc_y * filt_acc_y + filt_acc_z * filt_acc_z);

    MoveDirection temp_dir = DIR_NONE;
    
    if (*acc_num >= MOVE_VALID_THRESH) 
    {
//        // 斜向判断（X和Y轴均达到稳定阈值）
//        if ((state->vel_x > VELOCITY_THRESH || state->vel_x < -VELOCITY_THRESH) && 
//            (state->vel_y > VELOCITY_THRESH || state->vel_y < -VELOCITY_THRESH)) 
//        {
//            if (state->vel_x > VELOCITY_THRESH && state->vel_y > VELOCITY_THRESH)
//                temp_dir = DIR_UP_LEFT;
//            else if (state->vel_x < -VELOCITY_THRESH && state->vel_y > VELOCITY_THRESH)
//                temp_dir = DIR_UP_RIGHT;
//            else if (state->vel_x > VELOCITY_THRESH && state->vel_y < -VELOCITY_THRESH)
//                temp_dir = DIR_DOWN_LEFT;
//            else if (state->vel_x < -VELOCITY_THRESH && state->vel_y < -VELOCITY_THRESH)
//                temp_dir = DIR_DOWN_RIGHT;
//        }
        // 纯方向判断（仅单一轴达到稳定阈值）
        if (state->vel_x > VELOCITY_THRESH)
            temp_dir = DIR_LEFT;
        else if (state->vel_x < -VELOCITY_THRESH)
            temp_dir = DIR_RIGHT;
        else if (state->vel_y > VELOCITY_THRESH*3)
            temp_dir = DIR_UP;
        else if (state->vel_y < -VELOCITY_THRESH*3)
            temp_dir = DIR_DOWN;
    }
    else  // 无有效运动，速度快速归零（用慢衰减，避免残留）
    {
        temp_dir = DIR_NONE;
        state->vel_x *= DECAY_SLOW;
        state->vel_y *= DECAY_SLOW;
        if (fabs(state->vel_x) < 0.1f) state->vel_x = 0.0f;
        if (fabs(state->vel_y) < 0.1f) state->vel_y = 0.0f;
    }
    
    return temp_dir;
}

/**
 * @brief  更新方向并处理防抖
 * @param  state: 状态结构体指针
 * @param  new_dir: 新检测到的方向
 */
static void GestureMove_UpdateDirection(GestureMoveState *state, MoveDirection new_dir)
{
    if (new_dir == state->current_dir) 
    {
        state->dir_stable_cnt++;
    }
    else 
    {
        state->dir_stable_cnt = 0;  // 方向变化，重置计数器（切换回防抖模式）
    }

    state->current_dir = new_dir;  // 同步更新当前方向
}

/**
 * @brief  将方向映射到命令并发送
 * @param  state: 状态结构体指针
 * @param  filt_acc_x: 滤波后的X轴加速度（用于调试打印）
 * @param  filt_acc_y: 滤波后的Y轴加速度（用于调试打印）
 * @param  decay: 当前衰减系数（用于调试打印）
 * @param  acc_num: 总加速度（用于调试打印）
 */
static void GestureMove_MapToCommand(GestureMoveState *state, float filt_acc_x, float filt_acc_y, float decay, float acc_num)
{
    if (allow_send_flag == 1)
    {
//        switch (state->current_dir) 
//        {
//            case DIR_NONE:      current_cmd = CMD_NONE;    																				        break;
//            case DIR_UP:        current_cmd = CMD_UP;      printf("方向：上（衰减模式：响应）\r\n");        break;
//            case DIR_DOWN:      current_cmd = CMD_DOWN;    printf("方向：下（衰减模式：响应）\r\n");        break;
//            case DIR_LEFT:      current_cmd = CMD_LEFT;    printf("方向：左（衰减模式：响应）\r\n");        break;
//            case DIR_RIGHT:     current_cmd = CMD_RIGHT;   printf("方向：右（衰减模式：响应）\r\n");        break;
//            case DIR_UP_LEFT:   current_cmd = CMD_LEFTUP;  printf("方向：左上（衰减模式：响应）\r\n");      break;
//            case DIR_UP_RIGHT:  current_cmd = CMD_RIGHTUP; printf("方向：右上（衰减模式：响应）\r\n");      break;
//            case DIR_DOWN_LEFT: current_cmd = CMD_LEFTDOWN;printf("方向：左下（衰减模式：响应）\r\n");      break;
//            case DIR_DOWN_RIGHT:current_cmd = CMD_RIGHTDOWN;printf("方向：右下（衰减模式：响应）\r\n");    break;
//        }
			
				switch (state->current_dir) 
        {
            case DIR_NONE:      current_cmd = CMD_NONE;   						break;
            case DIR_UP:        current_cmd = CMD_UP;      		        break;
            case DIR_DOWN:      current_cmd = CMD_DOWN;    		        break;
            case DIR_LEFT:      current_cmd = CMD_LEFT;    		       	break;
            case DIR_RIGHT:     current_cmd = CMD_RIGHT;   		        break;
           
        }
        state->dir_stable_cnt = 0;  // 重置计数器，等待下一次稳定方向    
				if(trans_flag == 1)
				{
					switch (current_cmd) 
						{
								case CMD_NONE:    																				        																break;
								case CMD_UP:        HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xffff);        break;
								case CMD_DOWN:      HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xffff);        break;
								case CMD_LEFT:      HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xffff);       	break;
								case CMD_RIGHT:     HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xffff);        break;
								case CMD_LEFTUP:    HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xffff);      	break;
								case CMD_RIGHTUP:  	HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xffff);      	break;
								case CMD_LEFTDOWN: 	HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xffff);      	break;
								case CMD_RIGHTDOWN:	HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xffff);    		break;
						}
					trans_flag = 0;
				}

//        // 调试打印：显示关键参数+当前衰减系数
//        printf("加速度 X:%.2f, Y:%.2f | 速度 X:%.2f, Y:%.2f | 生效方向:%d | 衰减系数:%.1f | acc_num:%.2f\r\n", 
//               filt_acc_x, filt_acc_y, state->vel_x, state->vel_y, state->current_dir, decay, acc_num);

//				HAL_UART_Transmit(&huart6, &current_cmd, sizeof(current_cmd), 0xffff);
			}  
}
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************
//***************************************************************************************************************************************

/*
全部都要重写了>__<
					

*/
void Gesture_Direction(void)
{
		if(jy901s_data.angle_pitch > 30 && fabs(jy901s_data.angle_roll) < 10)
		{
			current_cmd = CMD_LEFT;
		}
		else if(jy901s_data.angle_pitch < -30 && fabs(jy901s_data.angle_roll) < 10)
		{
			current_cmd = CMD_RIGHT;
		}
		else if(jy901s_data.angle_roll > 40 && fabs(jy901s_data.angle_pitch) < 10)
		{
			current_cmd = CMD_UP;  
		}
		else if(jy901s_data.angle_roll < -40 && fabs(jy901s_data.angle_pitch) < 10)
		{
			current_cmd = CMD_DOWN; 
		}
		else
		{	
			current_cmd = CMD_NONE;
		}
}
/**
 * @brief  8方向运动识别（上下左右、左上、左下、右上、右下）
 * @note   基于JY901S加速度计的加速度差值进行方向判断
 *         此函数已从mpu6050.c移至control.c以提高代码可移植性
 *         已拆分为多个独立函数以提高复用性和可维护性
 */
// ====== 可调参数（按你的实际调用周期调） ======
#define PITCH_TH        25.0f
#define ROLL_TH         25.0f
#define DEAD_ZONE       15.0f
#define STABLE_COUNT_N  3
static uint8_t stable_cnt = 0;

// 根据当前姿态“实时判定”一个候选命令（不做稳定）
static uint8_t Gesture_DetectRaw(void)
{
    float pitch = jy901s_data.angle_pitch;
    float roll  = jy901s_data.angle_roll;

    if (pitch >  PITCH_TH && fabsf(roll) < DEAD_ZONE) return CMD_RIGHT;
    if (pitch < -PITCH_TH && fabsf(roll) < DEAD_ZONE) return CMD_LEFT;
    if (roll  >  ROLL_TH  && fabsf(pitch) < DEAD_ZONE) return CMD_UP;
    if (roll  < -ROLL_TH  && fabsf(pitch) < DEAD_ZONE) return CMD_DOWN;

    return CMD_NONE;
}
void Gesture_Move(void)
{
		if(jy901s_data.angle_pitch > 25 && fabs(jy901s_data.angle_roll) < 10)
		{
			current_cmd = CMD_RIGHT;
		}
		else if(jy901s_data.angle_pitch < -25 && fabs(jy901s_data.angle_roll) < 10)
		{
			current_cmd = CMD_LEFT;
		}
		else if(jy901s_data.angle_roll > 25 && fabs(jy901s_data.angle_pitch) < 10)
		{
			current_cmd = CMD_UP;  
		}
		else if(jy901s_data.angle_roll < -25 && fabs(jy901s_data.angle_pitch) < 10)
		{
			current_cmd = CMD_DOWN; 
		}
		else
		{	
			current_cmd = CMD_NONE;
		}
}
