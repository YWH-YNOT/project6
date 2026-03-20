/**
 * gesture_model.h
 * ------------------------------------------------------------
 * 由 uart/tools/train_svm.py 自动生成。
 * 重新训练后，脚本会同步覆盖 tools/generated 和 src/gesture 两份模型头文件。
 * 交叉验证平均准确率: 99.74%
 */
#ifndef GESTURE_MODEL_H
#define GESTURE_MODEL_H

#include <stddef.h>
#include <stdint.h>

#define GM_N_FEATURES 10
#define GM_N_CLASSES 10
#define GM_INVALID_LABEL 0xFFU

/* StandardScaler 参数 */
static const float gm_mean[10] = {-10.869937f, 29.850071f, -17.387766f, 15.266633f, -9.095820f, 2.869647f, 7.868477f, 3.069916f, 46.640736f, 9.272843f};
static const float gm_scale[10] = {34.820744f, 20.053808f, 38.050068f, 26.519682f, 44.293034f, 23.510532f, 47.084450f, 26.302752f, 30.879519f, 27.889488f};

/* LinearSVC 权重与偏置 */
static const float gm_W[10][10] =
{
    {2.979461f, 1.204022f, -0.364060f, -1.757906f, 2.882014f, 1.365262f, -1.344890f, 2.378163f, -0.536826f, -2.397465f},
    {-0.903286f, -0.336096f, 2.308702f, 1.991291f, -0.408760f, -3.517611f, 0.381778f, -3.635044f, -2.614746f, 5.851433f},
    {-0.773068f, 0.684000f, -0.012522f, -0.240972f, -0.756223f, -0.225444f, 0.763081f, 0.031375f, 0.100065f, 0.027307f},
    {0.028508f, 0.403555f, -1.210857f, 0.123540f, 1.006288f, 0.271753f, 0.368942f, -0.161377f, 0.057995f, -0.110790f},
    {0.889373f, -0.863766f, -0.328866f, 0.460442f, -0.562359f, -0.019019f, -0.047418f, 0.055383f, -0.218334f, -0.070230f},
    {0.078824f, 0.260161f, -0.836242f, 0.207697f, -0.065124f, 0.080862f, -0.840987f, -0.236546f, 0.400828f, -0.231269f},
    {-0.498901f, 0.010776f, -0.172455f, 0.071319f, 0.734914f, 0.104542f, 1.080901f, 0.094733f, -1.508522f, 0.124589f},
    {-0.216117f, -0.385244f, 0.010558f, -0.467852f, -0.000600f, -0.167332f, 0.176068f, -0.003273f, 0.038600f, -0.502892f},
    {0.113035f, 0.055608f, 0.001922f, 0.136297f, 0.143069f, 0.339964f, -0.222265f, 0.266510f, 0.149106f, 0.376848f},
    {-1.191625f, -0.242945f, 1.002143f, 0.140226f, 0.176870f, 0.113990f, 0.102224f, -0.091446f, 0.617821f, 0.130591f}
};
static const float gm_b[10] = {-4.773757f, -2.829417f, -1.426603f, -1.379397f, -1.411319f, -1.464720f, -2.543400f, -1.706984f, -1.110114f, -1.714671f};

/* 标签名称，方便调试打印 */
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
    "right",
    "three",
};

static inline uint8_t GestureModel_Classify(float const feat[GM_N_FEATURES])
{
    float best_score = -1e30f;
    uint8_t best_label = GM_INVALID_LABEL;

    if (NULL == feat)
    {
        return GM_INVALID_LABEL;
    }

    for (uint8_t cls = 0U; cls < GM_N_CLASSES; cls++)
    {
        float score = gm_b[cls];

        for (uint8_t i = 0U; i < GM_N_FEATURES; i++)
        {
            float normalized = (feat[i] - gm_mean[i]) / gm_scale[i];
            score += gm_W[cls][i] * normalized;
        }

        if (score > best_score)
        {
            best_score = score;
            best_label = cls;
        }
    }

    return best_label;
}

static inline uint8_t Gesture_Classify(
    float f0x, float f0y, float f1x, float f1y, float f2x,
    float f2y, float f3x, float f3y, float roll, float pitch)
{
    float feat[GM_N_FEATURES] =
    {
        f0x, f0y, f1x, f1y, f2x,
        f2y, f3x, f3y, roll, pitch
    };

    return GestureModel_Classify(feat);
}

#endif /* GESTURE_MODEL_H */