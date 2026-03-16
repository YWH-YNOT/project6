"""
train_svm.py — 手势分类器训练 & C代码导出工具
================================================
用法：
  python train_svm.py [--data gesture_data.csv] [--out gesture_model.h]

流程：
  1. 读取 collect_data.py 采集的 CSV
  2. 特征标准化（StandardScaler）
  3. 训练 LinearSVC（线性SVM）
  4. 5折交叉验证打印准确率
  5. 将模型权重导出为 gesture_model.h（C语言数组）
       → 直接 #include 到 STM32 固件中，无需外部库

依赖：
  pip install scikit-learn numpy pandas
"""

import argparse
import os
import sys
import numpy as np

# ---------------------------------------------------------------------------
# 手势名称表（9类统一：原有5种 + 方呈4种）
# ---------------------------------------------------------------------------
GESTURE_NAMES = {
    0: "fist",
    1: "open",
    2: "one",
    3: "two",
    4: "rock",
    5: "up",
    6: "down",
    7: "left",
    8: "right",
}
N_FEATURES = 10  # [f0x,f0y,f1x,f1y,f2x,f2y,f3x,f3y,roll,pitch]  (ch4 disabled)
N_CLASSES  = len(GESTURE_NAMES)


def load_data(csv_path: str):
    """自动检测CSV格式（有无表头、6/10/12维），兼容所有历史数据"""
    global N_FEATURES
    try:
        import pandas as pd
    except ImportError:
        print("[错误] 请安装 pandas：pip install pandas")
        sys.exit(1)

    if not os.path.exists(csv_path):
        print(f"[错误] 找不到数据文件：{csv_path}")
        sys.exit(1)

    df_peek = pd.read_csv(csv_path, nrows=1)
    first_col = df_peek.columns[0]
    has_header = True
    try:
        float(first_col)   # 如果列名能解析为数字 → 无表头
        has_header = False
    except ValueError:
        pass

    # 按有无表头重新读取
    if has_header:
        df = pd.read_csv(csv_path)
        n_cols = len(df.columns)
    else:
        df = pd.read_csv(csv_path, header=None)
        n_cols = len(df.columns)
        # 按列数分配列名（最后一列 = label）
        if n_cols == 7:       # 6维旧格式 + label
            df.columns = ["f0","f1","f2","f3","roll","pitch","label"]
        elif n_cols == 11:    # 10维新格式 + label
            df.columns = ["f0x","f0y","f1x","f1y","f2x","f2y","f3x","f3y","roll","pitch","label"]
        elif n_cols == 13:    # 12维（含ch4）+ label
            df.columns = ["f0x","f0y","f1x","f1y","f2x","f2y","f3x","f3y","f4x","f4y","roll","pitch","label"]
        else:
            print(f"[错误] 不支持的列数：{n_cols}（期望 7/11/13）")
            sys.exit(1)
        print(f"[信息] 检测到无表头 CSV，按 {n_cols} 列自动分配列名")

    # 按列名识别特征列
    cols = df.columns.tolist()
    if "f0x" in cols and "f4x" in cols:
        FEATURE_COLS = ["f0x","f0y","f1x","f1y","f2x","f2y","f3x","f3y","f4x","f4y","roll","pitch"]
        N_FEATURES = 12
        print("[信息] 12维格式（含通道4）")
    elif "f0x" in cols:
        FEATURE_COLS = ["f0x","f0y","f1x","f1y","f2x","f2y","f3x","f3y","roll","pitch"]
        N_FEATURES = 10
        print("[信息] 10维格式（标准）")
    elif "f0" in cols:
        FEATURE_COLS = ["f0","f1","f2","f3","roll","pitch"]
        N_FEATURES = 6
        print("[信息] 6维旧格式")
    else:
        print(f"[错误] 无法识别列名：{cols}")
        sys.exit(1)

    if "label" not in df.columns:
        print("[错误] CSV 缺少 label 列")
        sys.exit(1)

    # 过滤含 NaN 的行（文件末尾不完整行）
    before = len(df)
    df = df.dropna()
    dropped = before - len(df)
    if dropped > 0:
        print(f"[清理] 丢弃 {dropped} 行不完整数据（NaN）")

    X = df[FEATURE_COLS].values.astype(np.float32)
    y = df["label"].values.astype(int)

    print(f"[数据] {len(X)} 帧，{N_FEATURES} 维特征，各类别：")
    all_labels = sorted(set(y.tolist()))
    for label in all_labels:
        name = GESTURE_NAMES.get(label, f"label{label}")
        cnt  = int((y == label).sum())
        print(f"  [{label}] {name:12s}: {cnt} 帧")

    return X, y



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

    # 5折交叉验证
    cv = StratifiedKFold(n_splits=5, shuffle=True, random_state=42)
    scores = cross_val_score(pipeline, X, y, cv=cv, scoring="accuracy")
    print(f"\n[评估] 5折交叉验证准确率：")
    for i, s in enumerate(scores):
        print(f"  第{i+1}折: {s*100:.1f}%")
    print(f"  平均: {scores.mean()*100:.1f}%  ± {scores.std()*100:.1f}%")

    if scores.mean() < 0.85:
        print("\n[警告] 平均准确率 < 85%，建议：")
        print("  1. 增加每类采集帧数（目标 ≥ 200 帧/类）")
        print("  2. 检查采集时手势是否标准")

    # 最终用全量数据训练
    pipeline.fit(X, y)
    scaler = pipeline.named_steps["scaler"]
    svm    = pipeline.named_steps["svm"]

    return scaler, svm, scores.mean()


def export_c_header(scaler, svm, output_path: str, accuracy: float):
    """
    将 StandardScaler 和 LinearSVC 的参数导出为 C 头文件。

    LinearSVC 决策函数：
      score[k] = W[k] · x_norm + b[k]
      x_norm[i] = (x[i] - mean[i]) / scale[i]
      predicted = argmax(score)
    """
    mean_  = scaler.mean_.astype(np.float32)
    scale_ = scaler.scale_.astype(np.float32)
    W      = svm.coef_.astype(np.float32)    # shape: (n_classes, n_features)
    b      = svm.intercept_.astype(np.float32)  # shape: (n_classes,)

    def arr_to_c(arr, name, dtype="float"):
        """将 numpy 数组转为 C 初始化列表"""
        flat = arr.flatten()
        vals = ", ".join(f"{v:.6f}f" for v in flat)
        if arr.ndim == 1:
            return f"static const {dtype} {name}[{len(flat)}] = {{{vals}}};"
        else:
            rows, cols = arr.shape
            return f"static const {dtype} {name}[{rows}][{cols}] = {{{vals}}};"

    lines = [
        "/**",
        " * gesture_model.h — Auto-generated by train_svm.py",
        f" * Accuracy: {accuracy*100:.1f}%",
        f" * Features: {N_FEATURES} (f0x,f0y,f1x,f1y,f2x,f2y,f3x,f3y,roll,pitch)",
        f" * Classes : {N_CLASSES} ({', '.join(GESTURE_NAMES.values())})",
        " *",
        " * Usage in STM32:",
        " *   #include \"gesture_model.h\"",
        " *   uint8_t label = Gesture_Classify(f0,f1,f2,f3,roll,pitch);",
        " */",
        "#ifndef GESTURE_MODEL_H",
        "#define GESTURE_MODEL_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define GM_N_FEATURES {N_FEATURES}",
        f"#define GM_N_CLASSES  {N_CLASSES}",
        "",
        "/* --- StandardScaler 参数 --- */",
        arr_to_c(mean_,  "gm_mean"),
        arr_to_c(scale_, "gm_scale"),
        "",
        "/* --- LinearSVC 权重 W[class][feature] 和偏置 b[class] --- */",
        arr_to_c(W, "gm_W"),
        arr_to_c(b, "gm_b"),
        "",
        "/* --- 手势名称（调试用） --- */",
        'static const char * const gm_labels[GM_N_CLASSES] = {',
    ]
    for k in sorted(GESTURE_NAMES.keys()):
        lines.append(f'    "{GESTURE_NAMES[k]}",')
    # 根据特征数动态生成签名
    if N_FEATURES == 6:
        func_params = "float f0, float f1, float f2, float f3, float roll, float pitch"
        func_args   = "f0, f1, f2, f3, roll, pitch"
    elif N_FEATURES == 10:
        func_params = "float f0x, float f0y, float f1x, float f1y, float f2x, float f2y, float f3x, float f3y, float roll, float pitch"
        func_args   = "f0x, f0y, f1x, f1y, f2x, f2y, f3x, f3y, roll, pitch"
    elif N_FEATURES == 12:
        func_params = "float f0x, float f0y, float f1x, float f1y, float f2x, float f2y, float f3x, float f3y, float f4x, float f4y, float roll, float pitch"
        func_args   = "f0x, f0y, f1x, f1y, f2x, f2y, f3x, f3y, f4x, f4y, roll, pitch"

    lines += [
        "};",
        "",
        "/**",
        " * @brief  SVM 手势分类推理（纯C，无需外部库）",
        f" * @param  {func_args}",
        " * @retval 手势类别编号（0~N_CLASSES-1），失败返回 0xFF",
        " */",
        "static inline uint8_t Gesture_Classify(",
        f"    {func_params})",
        "{",
        f"    float feat[GM_N_FEATURES] = {{{func_args}}};",
        "    float best_score = -1e30f;",
        "    uint8_t best_cls = 0xFF;",
        "    for (uint8_t k = 0; k < GM_N_CLASSES; k++) {",
        "        /* score = W[k] · x_norm + b[k] */",
        "        float score = gm_b[k];",
        "        for (uint8_t i = 0; i < GM_N_FEATURES; i++) {",
        "            float xn = (feat[i] - gm_mean[i]) / gm_scale[i];",
        "            score += gm_W[k][i] * xn;  /* k*N_FEATURES+i for flat array */",
        "        }",
        "        if (score > best_score) {",
        "            best_score = score;",
        "            best_cls   = k;",
        "        }",
        "    }",
        "    return best_cls;",
        "}",
        "",
        "#endif /* GESTURE_MODEL_H */",
    ]

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print(f"\n[导出] 已生成 {output_path}")
    print(f"  在 STM32 工程中添加该文件，然后调用：")
    print(f"    #include \"gesture_model.h\"")
    print(f"    uint8_t g = Gesture_Classify(")
    print(f"        mpu_data[0].filter_angle_x, mpu_data[0].filter_angle_y,")
    print(f"        mpu_data[1].filter_angle_x, mpu_data[1].filter_angle_y,")
    print(f"        mpu_data[2].filter_angle_x, mpu_data[2].filter_angle_y,")
    print(f"        mpu_data[3].filter_angle_x, mpu_data[3].filter_angle_y,")
    print(f"        jy901s_data.angle_roll, jy901s_data.angle_pitch);")

def main():
    parser = argparse.ArgumentParser(description="手势SVM分类器训练工具")
    parser.add_argument("--data", default="gesture_data.csv", help="输入CSV文件")
    parser.add_argument("--out",  default="gesture_model.h",  help="输出C头文件")
    args = parser.parse_args()

    print("=" * 50)
    print("  手势 SVM 分类器训练工具")
    print("=" * 50)

    X, y = load_data(args.data)
    scaler, svm, acc = train_and_evaluate(X, y)
    export_c_header(scaler, svm, args.out, acc)

    print("\n[完成] 请将生成的头文件复制到 STM32 工程的 HARDWARE/ 目录")


if __name__ == "__main__":
    main()
