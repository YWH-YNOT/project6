#ifndef GESTURE_GESTURE_MODEL_H_
#define GESTURE_GESTURE_MODEL_H_

#include <stddef.h>
#include <stdint.h>

/*
 * 这一份模型参数直接继承 STM32 端当前已经验证通过的 9 类手势 SVM 模型。
 * 先保证“同一组输入特征，在 RA6M5 上得到的标签结果与 STM32 一致”，
 * 后续如果你重新训练模型，只需要替换这一份头文件里的参数即可。
 */
#define GM_N_FEATURES    10U
#define GM_N_CLASSES     9U
#define GM_INVALID_LABEL 0xFFU

/*
 * StandardScaler 参数。
 * 分类前先做 (x - mean) / scale，与 PC 训练脚本和 STM32 端保持一致。
 */
static const float gm_mean[GM_N_FEATURES] =
{
    -25.101862f, 24.178146f, -37.274487f, 7.617027f, -22.354797f,
    1.451018f, -12.301026f, 1.354114f, 32.991261f, 6.068376f
};

static const float gm_scale[GM_N_FEATURES] =
{
    32.478775f, 21.176632f, 29.013323f, 23.492916f, 35.327015f,
    22.988407f, 42.474228f, 22.432766f, 29.686691f, 24.520857f
};

/*
 * 线性 SVM 权重矩阵和偏置项。
 * 每一行对应一个类别，每一列对应一个输入特征。
 */
static const float gm_W[GM_N_CLASSES][GM_N_FEATURES] =
{
    {-1.067263f, -2.792433f, -1.080884f, -1.704304f,  0.242090f,  0.119455f, -0.636904f, -3.398105f,  1.805671f,  8.153774f},
    { 0.412971f,  0.285017f,  0.954482f,  0.147183f,  0.395713f, -0.931799f, -0.569455f, -0.001715f, -0.226898f,  0.156015f},
    {-1.215665f,  0.418719f,  0.284910f, -0.015399f, -1.161343f,  0.092417f,  1.194421f, -0.066096f, -0.177673f,  0.060570f},
    {-0.196298f,  0.359113f, -0.618648f,  0.400950f,  1.114324f, -0.245582f,  0.149611f, -0.044346f, -0.135361f, -0.053144f},
    { 1.327772f, -0.400142f, -0.799202f,  0.169871f, -0.242309f, -0.013126f, -0.080036f, -0.123689f, -0.346037f, -0.042049f},
    { 0.242218f,  0.952151f, -0.446652f,  0.444484f, -1.191978f, -0.620049f, -1.084980f,  2.899781f, -1.023174f, -4.109328f},
    { 0.102201f, -0.306963f, -0.141128f,  0.048959f,  0.314245f,  0.106818f,  0.221662f,  0.142148f, -1.591639f,  0.247434f},
    {-0.480960f, -0.135778f,  0.273038f, -0.545839f, -0.182295f, -0.270014f,  0.204249f, -0.317281f, -0.175443f, -0.286617f},
    { 0.133080f,  0.332197f,  0.084391f,  0.375787f,  0.241076f,  0.425396f, -0.400358f,  0.294558f,  0.192690f,  0.478857f}
};

static const float gm_b[GM_N_CLASSES] =
{
    -1.939939f, -1.516030f, -1.377746f, -1.111945f, -1.170178f,
    -2.986409f, -1.907196f, -1.411201f, -1.488806f
};

/* 标签名字主要用于 RA 端串口调试输出。 */
static const char * const gm_labels[GM_N_CLASSES] =
{
    "fist",
    "open",
    "one",
    "two",
    "rock",
    "up",
    "down",
    "left",
    "right"
};

/*
 * 直接对 10 维数组做分类，更适合 RA 端当前“串口收包 -> 缓存特征数组”的数据流。
 * 这也是后面继续做滑窗、滤波、投票时最容易复用的接口形式。
 */
static inline uint8_t GestureModel_Classify(float const feature[GM_N_FEATURES])
{
    float   best_score = -1.0e30f;
    uint8_t best_label = GM_INVALID_LABEL;

    if (NULL == feature)
    {
        return GM_INVALID_LABEL;
    }

    for (uint8_t cls = 0U; cls < GM_N_CLASSES; cls++)
    {
        float score = gm_b[cls];

        for (uint8_t i = 0U; i < GM_N_FEATURES; i++)
        {
            float normalized = (feature[i] - gm_mean[i]) / gm_scale[i];
            score += gm_W[cls][i] * normalized;
        }

        if ((GM_INVALID_LABEL == best_label) || (score > best_score))
        {
            best_score = score;
            best_label = cls;
        }
    }

    return best_label;
}

/*
 * 保留 STM32 端原有的 10 参数接口，方便后续做一致性对照时直接复用旧调用方式。
 */
static inline uint8_t Gesture_Classify(
    float f0x,
    float f0y,
    float f1x,
    float f1y,
    float f2x,
    float f2y,
    float f3x,
    float f3y,
    float roll,
    float pitch)
{
    float feature[GM_N_FEATURES] = {f0x, f0y, f1x, f1y, f2x, f2y, f3x, f3y, roll, pitch};

    return GestureModel_Classify(feature);
}

#endif /* GESTURE_GESTURE_MODEL_H_ */
