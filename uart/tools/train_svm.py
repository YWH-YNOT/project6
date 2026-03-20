"""
train_svm.py
============
读取 `collect_data.py` 采集得到的 CSV，训练线性 SVM，并导出 C 头文件。

当前工具链已经升级为 10 类标签：
0~8 保持原有手势定义不变，`label=9` 对应 `three`。

导出结果默认会同时写入：
1. `uart/tools/generated/gesture_model.h`
2. `uart/src/gesture/gesture_model.h`

这样重新训练后不需要再手工复制模型头文件，重新编译工程即可生效。
"""

from __future__ import annotations

import argparse
import os
import sys
from typing import Iterable

import numpy as np
from tool_config import (
    DEFAULT_DATASET_PATH,
    DEFAULT_FIRMWARE_MODEL_PATH,
    DEFAULT_GENERATED_MODEL_PATH,
    FEATURE_NAMES,
    GESTURE_LABELS,
)


DEFAULT_DATA = DEFAULT_DATASET_PATH
DEFAULT_OUT = DEFAULT_GENERATED_MODEL_PATH
DEFAULT_FIRMWARE_OUT = DEFAULT_FIRMWARE_MODEL_PATH


def ensure_dependencies() -> None:
    """在真正训练前检查依赖，缺失时直接给出安装提示。"""
    try:
        import pandas  # noqa: F401
        import sklearn  # noqa: F401
    except ImportError as exc:
        print("[错误] 缺少训练依赖，请先安装：")
        print("  pip install numpy pandas scikit-learn")
        raise SystemExit(1) from exc


def load_dataset(csv_path: str) -> tuple[np.ndarray, np.ndarray]:
    """
    读取数据集并做基础校验：
    1. 支持带表头或不带表头的 CSV；
    2. 要求标签必须完整覆盖 0~9；
    3. 旧的 0~8 类历史数据会原样保留，只需要额外补齐 label=9 的 three 数据。
    """
    import pandas as pd

    if not os.path.exists(csv_path):
        print(f"[错误] 找不到数据文件: {csv_path}")
        sys.exit(1)

    df = pd.read_csv(csv_path)

    if not set(FEATURE_NAMES + ["label"]).issubset(df.columns):
        df = pd.read_csv(csv_path, header=None)
        if len(df.columns) != len(FEATURE_NAMES) + 1:
            print("[错误] CSV 列数不正确。")
            print(f"       期望 {len(FEATURE_NAMES) + 1} 列: 10 个特征 + 1 个 label")
            sys.exit(1)
        df.columns = FEATURE_NAMES + ["label"]

    df = df.dropna(subset=FEATURE_NAMES + ["label"])
    if df.empty:
        print("[错误] 数据文件为空，无法训练。")
        sys.exit(1)

    X = df[FEATURE_NAMES].astype(np.float32).to_numpy()
    y = df["label"].astype(int).to_numpy()

    expected_labels = list(GESTURE_LABELS.keys())
    actual_labels = sorted(set(y.tolist()))
    if actual_labels != expected_labels:
        print("[错误] 当前工程的命令映射已经升级为 10 类标签 0~9。")
        print(f"       实际检测到的标签: {actual_labels}")
        print("       旧数据会继续保留，请在同一份 CSV 中补齐 three(label=9) 后再训练。")
        sys.exit(1)

    print(f"[数据] 共 {len(X)} 帧，特征维度 {len(FEATURE_NAMES)}。")
    for label in expected_labels:
        count = int((y == label).sum())
        print(f"  [{label}] {GESTURE_LABELS[label]:>5s}: {count:4d} 帧")

    return X, y


def choose_n_splits(y: np.ndarray) -> int:
    """根据最少类别样本数自动选择可用的交叉验证折数。"""
    counts = [int((y == label).sum()) for label in GESTURE_LABELS]
    min_count = min(counts)
    if min_count < 2:
        print("[错误] 每一类至少需要 2 帧数据，当前最少类别不足。")
        sys.exit(1)
    return min(5, min_count)


def train_model(X: np.ndarray, y: np.ndarray):
    """完成标准化、线性 SVM 训练和交叉验证评估。"""
    from sklearn.model_selection import StratifiedKFold, cross_val_score
    from sklearn.pipeline import Pipeline
    from sklearn.preprocessing import StandardScaler
    from sklearn.svm import LinearSVC

    pipeline = Pipeline(
        [
            ("scaler", StandardScaler()),
            ("svm", LinearSVC(C=1.0, max_iter=8000, random_state=42)),
        ]
    )

    n_splits = choose_n_splits(y)
    cv = StratifiedKFold(n_splits=n_splits, shuffle=True, random_state=42)
    scores = cross_val_score(pipeline, X, y, cv=cv, scoring="accuracy")

    print(f"\n[评估] {n_splits} 折交叉验证准确率：")
    for index, score in enumerate(scores, start=1):
        print(f"  第 {index} 折: {score * 100:.2f}%")
    print(f"  平均值: {scores.mean() * 100:.2f}%")
    print(f"  标准差: {scores.std() * 100:.2f}%")

    pipeline.fit(X, y)
    scaler = pipeline.named_steps["scaler"]
    svm = pipeline.named_steps["svm"]
    return scaler, svm, float(scores.mean())


def format_c_1d(values: Iterable[float], name: str) -> str:
    """把一维浮点数组转成 C 头文件里的静态常量定义。"""
    values_list = list(values)
    content = ", ".join(f"{value:.6f}f" for value in values_list)
    return f"static const float {name}[{len(values_list)}] = {{{content}}};"


def format_c_2d(values: np.ndarray, name: str) -> str:
    """把二维浮点数组转成 C 头文件里的静态常量定义。"""
    rows = []
    for row in values:
        rows.append("    {" + ", ".join(f"{value:.6f}f" for value in row) + "}")
    body = ",\n".join(rows)
    return (
        f"static const float {name}[{values.shape[0]}][{values.shape[1]}] =\n"
        "{\n"
        f"{body}\n"
        "};"
    )


def write_header_file(output_path: str, header_text: str) -> None:
    """把生成好的模型头文件文本写到指定路径。"""
    output_dir = os.path.dirname(output_path)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    with open(output_path, "w", encoding="utf-8") as header_file:
        header_file.write(header_text)

    print(f"[导出] 已生成模型头文件: {os.path.abspath(output_path)}")


def export_header(
    scaler,
    svm,
    output_path: str,
    firmware_output_path: str | None,
    accuracy: float,
) -> None:
    """
    导出 C 权重文件。
    训练完成后会同时更新：
    1. `uart/tools/generated/gesture_model.h`，方便留档和比对；
    2. `uart/src/gesture/gesture_model.h`，保证固件编译时真正拿到最新模型。
    """
    mean_ = scaler.mean_.astype(np.float32)
    scale_ = scaler.scale_.astype(np.float32)
    coef_ = svm.coef_.astype(np.float32)
    bias_ = svm.intercept_.astype(np.float32)

    lines = [
        "/**",
        " * gesture_model.h",
        " * ------------------------------------------------------------",
        " * 由 uart/tools/train_svm.py 自动生成。",
        " * 重新训练后，脚本会同步覆盖 tools/generated 和 src/gesture 两份模型头文件。",
        f" * 交叉验证平均准确率: {accuracy * 100:.2f}%",
        " */",
        "#ifndef GESTURE_MODEL_H",
        "#define GESTURE_MODEL_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"#define GM_N_FEATURES {len(FEATURE_NAMES)}",
        f"#define GM_N_CLASSES {len(GESTURE_LABELS)}",
        "#define GM_INVALID_LABEL 0xFFU",
        "",
        "/* StandardScaler 参数 */",
        format_c_1d(mean_.tolist(), "gm_mean"),
        format_c_1d(scale_.tolist(), "gm_scale"),
        "",
        "/* LinearSVC 权重与偏置 */",
        format_c_2d(coef_, "gm_W"),
        format_c_1d(bias_.tolist(), "gm_b"),
        "",
        "/* 标签名称，方便调试打印 */",
        "static const char * const gm_labels[GM_N_CLASSES] =",
        "{",
    ]

    for label in range(len(GESTURE_LABELS)):
        lines.append(f'    "{GESTURE_LABELS[label]}",')

    lines.extend(
        [
            "};",
            "",
            "static inline uint8_t GestureModel_Classify(float const feat[GM_N_FEATURES])",
            "{",
            "    float best_score = -1e30f;",
            "    uint8_t best_label = GM_INVALID_LABEL;",
            "",
            "    if (NULL == feat)",
            "    {",
            "        return GM_INVALID_LABEL;",
            "    }",
            "",
            "    for (uint8_t cls = 0U; cls < GM_N_CLASSES; cls++)",
            "    {",
            "        float score = gm_b[cls];",
            "",
            "        for (uint8_t i = 0U; i < GM_N_FEATURES; i++)",
            "        {",
            "            float normalized = (feat[i] - gm_mean[i]) / gm_scale[i];",
            "            score += gm_W[cls][i] * normalized;",
            "        }",
            "",
            "        if (score > best_score)",
            "        {",
            "            best_score = score;",
            "            best_label = cls;",
            "        }",
            "    }",
            "",
            "    return best_label;",
            "}",
            "",
            "static inline uint8_t Gesture_Classify(",
            "    float f0x, float f0y, float f1x, float f1y, float f2x,",
            "    float f2y, float f3x, float f3y, float roll, float pitch)",
            "{",
            "    float feat[GM_N_FEATURES] =",
            "    {",
            "        f0x, f0y, f1x, f1y, f2x,",
            "        f2y, f3x, f3y, roll, pitch",
            "    };",
            "",
            "    return GestureModel_Classify(feat);",
            "}",
            "",
            "#endif /* GESTURE_MODEL_H */",
        ]
    )

    header_text = "\n".join(lines)
    output_paths = [output_path]

    if firmware_output_path:
        if os.path.abspath(firmware_output_path) not in {os.path.abspath(output_path)}:
            output_paths.append(firmware_output_path)

    print()
    for target_path in output_paths:
        write_header_file(target_path, header_text)

    if firmware_output_path:
        print("       固件实际使用的模型头文件也已同步更新，无需再手工复制。")


def parse_args() -> argparse.Namespace:
    """解析命令行参数。"""
    parser = argparse.ArgumentParser(description="训练线性 SVM 并导出 C 权重文件")
    parser.add_argument("--data", default=DEFAULT_DATA, help="输入 CSV 文件路径")
    parser.add_argument("--out", default=DEFAULT_OUT, help="输出头文件路径")
    parser.add_argument(
        "--firmware-out",
        default=DEFAULT_FIRMWARE_OUT,
        help="同步写入固件工程使用的模型头文件路径；传空字符串则只生成 --out",
    )
    return parser.parse_args()


def main() -> None:
    """训练工具主流程。"""
    args = parse_args()
    firmware_out = args.firmware_out.strip()
    ensure_dependencies()

    X, y = load_dataset(args.data)
    scaler, svm, accuracy = train_model(X, y)
    export_header(
        scaler=scaler,
        svm=svm,
        output_path=args.out,
        firmware_output_path=(firmware_out if firmware_out else None),
        accuracy=accuracy,
    )


if __name__ == "__main__":
    main()
