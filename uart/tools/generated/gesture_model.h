/**
 * gesture_model.h
 * ------------------------------------------------------------
 * 由 uart/tools/train_svm.py 自动生成。
 * 重新训练后，脚本会同步覆盖 tools/generated 和 src/gesture 两份模型头文件。
 * 交叉验证平均准确率: 99.69%
 */
#ifndef GESTURE_MODEL_H
#define GESTURE_MODEL_H

#include <stddef.h>
#include <stdint.h>

#define GM_N_FEATURES 10
#define GM_N_CLASSES 10
#define GM_INVALID_LABEL 0xFFU

/* StandardScaler 参数 */
static const float gm_mean[10] = {-10.033227f, 30.158579f, -16.234507f, 15.333071f, -8.091592f, 2.931703f, 9.267444f, 2.816977f, 46.529495f, 9.037208f};
static const float gm_scale[10] = {34.480118f, 20.149910f, 37.192745f, 26.711487f, 43.918728f, 23.689018f, 45.919567f, 26.517437f, 31.066454f, 28.150913f};

/* LinearSVC 权重与偏置 */
static const float gm_W[10][10] =
{
    {2.971261f, 1.211401f, -0.357439f, -1.759165f, 2.889588f, 1.370134f, -1.302724f, 2.378735f, -0.570542f, -2.388046f},
    {-0.897271f, -0.333784f, 2.274389f, 2.016375f, -0.421707f, -3.533427f, 0.377331f, -3.659786f, -2.636490f, 5.867123f},
    {-0.748132f, 0.700505f, -0.006819f, -0.258092f, -0.746588f, -0.252169f, 0.724852f, 0.016761f, 0.115976f, 0.024626f},
    {0.022362f, 0.415254f, -1.170018f, 0.145657f, 1.019147f, 0.187771f, 0.434455f, -0.185615f, -0.029946f, -0.124598f},
    {0.877045f, -0.930497f, -0.379331f, 0.367570f, -0.526814f, 0.049923f, -0.028896f, 0.059525f, -0.103058f, -0.037195f},
    {0.098699f, 0.299177f, -0.946141f, 0.151757f, -0.058340f, 0.203284f, -0.827524f, -0.251043f, 0.377982f, -0.247076f},
    {-0.491010f, 0.006973f, -0.141847f, 0.061003f, 0.736690f, 0.110951f, 1.062896f, 0.104101f, -1.518165f, 0.121458f},
    {-0.214831f, -0.410007f, -0.004634f, -0.430042f, 0.013227f, -0.170620f, 0.144799f, -0.038125f, 0.048286f, -0.470482f},
    {0.099975f, 0.043894f, 0.008672f, 0.153034f, 0.137743f, 0.341482f, -0.193140f, 0.276445f, 0.142139f, 0.381031f},
    {-1.181488f, -0.238189f, 0.983798f, 0.137351f, 0.176309f, 0.126210f, 0.104254f, -0.084152f, 0.611576f, 0.120515f}
};
static const float gm_b[10] = {-4.736147f, -2.804575f, -1.442566f, -1.446479f, -1.394226f, -1.553178f, -2.510566f, -1.689616f, -1.130080f, -1.714094f};

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