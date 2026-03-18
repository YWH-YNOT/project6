/**
 * gesture_model.h
 * ------------------------------------------------------------
 * 由 uart/tools/train_svm.py 自动生成。
 * 重新训练后，只需要替换本文件并重新编译 `uart` 工程即可。
 * 交叉验证平均准确率: 99.78%
 */
#ifndef GESTURE_MODEL_H
#define GESTURE_MODEL_H

#include <stddef.h>
#include <stdint.h>

#define GM_N_FEATURES 10
#define GM_N_CLASSES 9
#define GM_INVALID_LABEL 0xFFU

/* StandardScaler 参数 */
static const float gm_mean[10] = {-8.953590f, 29.247808f, -13.575206f, 14.995795f, -4.290358f, 2.870852f, 6.623789f, 2.667324f, 43.053417f, 8.758964f};
static const float gm_scale[10] = {34.816978f, 21.671402f, 34.060524f, 28.781170f, 41.181587f, 23.148592f, 43.415771f, 27.738831f, 32.224697f, 29.482098f};

/* LinearSVC 权重与偏置 */
static const float gm_W[9][10] =
{
    {2.987139f, 1.228652f, -0.308041f, -1.752460f, 2.978855f, 1.217722f, -1.281778f, 2.399118f, -0.687430f, -2.278951f},
    {-3.146355f, -1.014976f, 3.224241f, 3.288069f, -0.226507f, -3.324916f, -0.307569f, -3.419357f, -0.876772f, 5.393770f},
    {-0.773919f, 0.688367f, 0.165687f, -0.443122f, -0.675012f, -0.144182f, 0.648400f, 0.106461f, 0.216114f, -0.003552f},
    {-0.446142f, 0.323790f, -0.651306f, 0.113944f, 0.945036f, 0.215779f, 0.283497f, -0.086042f, 0.324769f, -0.093279f},
    {0.930447f, -0.886314f, -0.297891f, 0.416583f, -0.518100f, 0.057266f, -0.081370f, 0.070394f, -0.180835f, 0.019826f},
    {0.177639f, 0.341298f, -0.668256f, 0.191892f, -0.070916f, 0.022473f, -0.828831f, -0.188262f, 0.399476f, -0.342717f},
    {-0.011126f, -0.055535f, -0.144279f, 0.034619f, 0.237519f, 0.668186f, 0.275594f, -0.058095f, -0.922311f, 0.105503f},
    {-0.440952f, -0.466134f, 0.096774f, -0.383229f, -0.086173f, -0.153406f, 0.321123f, 0.017508f, 0.198964f, -0.438641f},
    {-0.001932f, -0.014957f, 0.029205f, 0.067767f, 0.116262f, 0.174435f, -0.109720f, 0.270471f, 0.169525f, 0.282301f}
};
static const float gm_b[9] = {-4.562298f, -1.530891f, -1.412187f, -1.208691f, -1.356090f, -1.500939f, -1.316426f, -1.396087f, -1.051081f};

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