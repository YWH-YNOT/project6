"""
train_swipe_svm.py — 挥手方向 SVM 分类器训练 & C代码导出
==========================================================
用法：
  python train_swipe_svm.py [--data swipe_data.csv] [--out swipe_model.h]

流程：
  1. 读取 collect_swipe.py 采集的 CSV（每行 = 30帧 × 4通道 + label）
  2. 提取每个样本的统计特征（24维）
  3. 训练 LinearSVC（线性SVM）
  4. 5折交叉验证打印准确率
  5. 将模型权重导出为 swipe_model.h（C语言，直接 #include 到 STM32）

特征定义（每个方向通道 6 个统计量 × 4 通道 = 24 维）：
  max, min, range, mean, delta(末-初), peak_pos(峰值位置 0~1)
  通道顺序：roll, pitch, gyro_z, gyro_y
"""

import argparse
import os
import sys
import numpy as np

# ---------------------------------------------------------------------------
SWIPE_NAMES = {0: "left", 1: "right", 2: "up", 3: "down"}
CHANNEL_NAMES = ["roll", "pitch", "gyro_z", "gyro_y"]
N_CHANNELS  = 4
WINDOW_SIZE = 40  # 与 collect_swipe.py 保持一致
N_FEATURES  = N_CHANNELS * 6   # 24维特征
N_CLASSES   = 4
# ---------------------------------------------------------------------------


def extract_features(window: np.ndarray) -> np.ndarray:
    """
    从形状 (WINDOW_SIZE, N_CHANNELS) 的时间窗口中提取统计特征。
    每通道 6 个特征：max, min, range, mean, delta, peak_pos
    Returns: numpy array of shape (N_FEATURES,)
    """
    feats = []
    for ch in range(N_CHANNELS):
        x = window[:, ch]
        x_max  = float(np.max(x))
        x_min  = float(np.min(x))
        x_mean = float(np.mean(x))
        x_rng  = x_max - x_min
        x_dlt  = float(x[-1] - x[0])                   # 末帧 - 首帧
        pk_pos = float(np.argmax(np.abs(x))) / max(len(x) - 1, 1)  # 0~1
        feats.extend([x_max, x_min, x_rng, x_mean, x_dlt, pk_pos])
    return np.array(feats, dtype=np.float32)


def load_data(csv_path: str):
    """加载 CSV 并提取特征矩阵 X 和标签向量 y（兼容有/无表头两种格式）"""
    try:
        import pandas as pd
    except ImportError:
        print("[错误] 请安装 pandas：pip install pandas")
        sys.exit(1)

    if not os.path.exists(csv_path):
        print(f"[错误] 找不到数据文件：{csv_path}")
        sys.exit(1)

    expected_raw = WINDOW_SIZE * N_CHANNELS   # 120

    # ---- 尝试判断是否有表头 ----
    df_peek = pd.read_csv(csv_path, nrows=1)
    df_peek.columns = df_peek.columns.str.strip()
    has_label = any(c.lower() == "label" for c in df_peek.columns)
    # 如果列数等于 expected_raw+1 且有 label 列 → 有表头
    # 如果列名本身是数字（第一行是数据）→ 无表头
    try:
        float(df_peek.columns[0])   # 列名能解析为数字 → 无表头
        is_headerless = True
    except ValueError:
        is_headerless = not has_label

    if is_headerless:
        print("[信息] 检测到无表头 CSV，按列数自动解析")
        df = pd.read_csv(csv_path, header=None)
        if len(df.columns) != expected_raw + 1:
            print(f"[错误] 期望 {expected_raw+1} 列，实际 {len(df.columns)} 列")
            sys.exit(1)
        label_col = df.columns[-1]
        raw_cols  = df.columns[:-1].tolist()
    else:
        df = pd.read_csv(csv_path)
        df.columns = df.columns.str.strip()
        label_col = next((c for c in df.columns if c.lower() == "label"), None)
        if label_col is None:
            print("[错误] CSV 中找不到 label 列")
            sys.exit(1)
        raw_cols = [c for c in df.columns if c != label_col]

    print(f"[数据] 加载 {len(df)} 个样本，{len(df.columns)} 列")

    n_raw_cols = len(raw_cols)
    if n_raw_cols != expected_raw:
        print(f"[错误] 期望 {expected_raw} 列原始数据，实际 {n_raw_cols} 列")
        sys.exit(1)

    raw    = df[raw_cols].values.astype(np.float32)   # (N, 120)
    labels = df[label_col].values.astype(int)

    # 重塑为 (N, WINDOW_SIZE, N_CHANNELS) 再提取特征
    windows = raw.reshape(-1, WINDOW_SIZE, N_CHANNELS)
    X = np.stack([extract_features(w) for w in windows])

    print(f"[特征] 提取完成，形状 X={X.shape}")
    print(f"\n各方向样本数：")
    for lbl, name in SWIPE_NAMES.items():
        cnt = np.sum(labels == lbl)
        print(f"  [{lbl}] {name:8s}: {cnt} 个")

    return X, labels


def train_and_evaluate(X, y):
    try:
        from sklearn.svm import LinearSVC
        from sklearn.preprocessing import StandardScaler
        from sklearn.pipeline import Pipeline
        from sklearn.model_selection import cross_val_score, StratifiedKFold
    except ImportError:
        print("[错误] 请安装 scikit-learn：pip install scikit-learn")
        sys.exit(1)

    pipeline = Pipeline([
        ("scaler", StandardScaler()),
        ("svm",    LinearSVC(C=1.0, max_iter=5000, random_state=42)),
    ])

    cv = StratifiedKFold(n_splits=5, shuffle=True, random_state=42)
    scores = cross_val_score(pipeline, X, y, cv=cv, scoring="accuracy")

    print(f"\n[评估] 5折交叉验证准确率：")
    for i, s in enumerate(scores):
        print(f"  第{i+1}折: {s*100:.1f}%")
    print(f"  平均: {scores.mean()*100:.1f}%  ± {scores.std()*100:.1f}%")

    if scores.mean() < 0.85:
        print("\n[警告] 准确率 < 85%，建议：")
        print("  1. 增加每方向样本数（目标 ≥ 100 个）")
        print("  2. 检查采集时序——挥完手后要立即按键")
        print("  3. 保持挥手幅度一致")

    pipeline.fit(X, y)
    scaler = pipeline.named_steps["scaler"]
    svm    = pipeline.named_steps["svm"]
    return scaler, svm, scores.mean()


def export_c_header(scaler, svm, output_path: str, accuracy: float):
    """
    导出 swipe_model.h：
      - StandardScaler 均值/标准差
      - LinearSVC 权重矩阵 W[class][feature] 和偏置 b[class]
      - Swipe_Classify() 内联推理函数
    """
    mean_  = scaler.mean_.astype(np.float32)
    scale_ = scaler.scale_.astype(np.float32)
    W      = svm.coef_.astype(np.float32)         # (N_CLASSES, N_FEATURES)
    b      = svm.intercept_.astype(np.float32)    # (N_CLASSES,)

    def arr1d(arr, name):
        vals = ", ".join(f"{v:.6f}f" for v in arr.flatten())
        return f"static const float {name}[{len(arr)}] = {{{vals}}};"

    def arr2d(arr, name):
        rows, cols = arr.shape
        vals = ", ".join(f"{v:.6f}f" for v in arr.flatten())
        return f"static const float {name}[{rows}][{cols}] = {{{vals}}};"

    # 特征名称注释
    feat_comment = []
    for ch in CHANNEL_NAMES:
        for stat in ["max", "min", "range", "mean", "delta", "peak_pos"]:
            feat_comment.append(f"{ch}_{stat}")

    lines = [
        "/**",
        " * swipe_model.h — Auto-generated by train_swipe_svm.py",
        f" * Accuracy : {accuracy*100:.1f}%",
        f" * Features : {N_FEATURES} ({', '.join(feat_comment)})",
        f" * Classes  : {N_CLASSES} ({', '.join(SWIPE_NAMES.values())})",
        " *",
        " * STM32 用法：",
        " *   #include \"swipe_model.h\"",
        " *   // 准备 float feat[SM_N_FEATURES];",
        " *   // 填入从 WINDOW_SIZE 帧中提取的 24 维特征",
        " *   uint8_t dir = Swipe_Classify(feat);",
        " */",
        "#ifndef SWIPE_MODEL_H",
        "#define SWIPE_MODEL_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define SM_N_FEATURES  {N_FEATURES}",
        f"#define SM_N_CLASSES   {N_CLASSES}",
        f"#define SM_WINDOW_SIZE {WINDOW_SIZE}",
        f"#define SM_N_CHANNELS  {N_CHANNELS}",
        "",
        "/* StandardScaler 参数 */",
        arr1d(mean_,  "sm_mean"),
        arr1d(scale_, "sm_scale"),
        "",
        "/* LinearSVC 权重 W[class][feature] 和偏置 b[class] */",
        arr2d(W, "sm_W"),
        arr1d(b, "sm_b"),
        "",
        "/* 方向名称（调试用） */",
        "static const char * const sm_labels[SM_N_CLASSES] = {",
    ]
    for k in sorted(SWIPE_NAMES.keys()):
        lines.append(f'    "{SWIPE_NAMES[k]}",')
    lines += [
        "};",
        "",
        "/**",
        " * @brief  从24维特征向量推理挥手方向",
        " * @param  feat  长度为 SM_N_FEATURES 的特征数组",
        " *               特征提取方式见 Swipe_ExtractFeatures() in control.c",
        " * @retval 0=left 1=right 2=up 3=down，失败返回 0xFF",
        " */",
        "static inline uint8_t Swipe_Classify(const float feat[SM_N_FEATURES])",
        "{",
        "    float best_score = -1e30f;",
        "    uint8_t best_cls = 0xFF;",
        "    for (uint8_t k = 0; k < SM_N_CLASSES; k++) {",
        "        float score = sm_b[k];",
        "        for (uint8_t i = 0; i < SM_N_FEATURES; i++) {",
        "            float xn = (feat[i] - sm_mean[i]) / sm_scale[i];",
        "            score += sm_W[k][i] * xn;",
        "        }",
        "        if (score > best_score) {",
        "            best_score = score;",
        "            best_cls   = k;",
        "        }",
        "    }",
        "    return best_cls;",
        "}",
        "",
        "#endif /* SWIPE_MODEL_H */",
    ]

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print(f"\n[导出] 已生成 {output_path}")
    print(f"  将文件复制到 STM32 工程的 HARDWARE/ 目录，然后：")
    print(f"    #include \"swipe_model.h\"")
    print(f"  在 control.c 的 Gesture_Move() 中调用 Swipe_Classify(feat)")


def main():
    parser = argparse.ArgumentParser(description="挥手方向 SVM 分类器训练工具")
    parser.add_argument("--data", default="swipe_data.csv",  help="输入CSV文件")
    parser.add_argument("--out",  default="swipe_model.h",   help="输出C头文件")
    args = parser.parse_args()

    print("=" * 50)
    print("  挥手方向 SVM 分类器训练工具")
    print("=" * 50)

    X, y = load_data(args.data)
    scaler, svm, acc = train_and_evaluate(X, y)
    export_c_header(scaler, svm, args.out, acc)

    print("\n[完成] 请将 swipe_model.h 复制到 STM32 工程的 HARDWARE/ 目录")


if __name__ == "__main__":
    main()
