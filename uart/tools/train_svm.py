"""
train_svm.py
============
Train a linear SVM from `collect_data.py` CSV output and export C headers.

The script is designed to be resilient on Windows machines where the global
Python or Conda environment may be polluted by incompatible binary packages.
If `numpy` / `scikit-learn` cannot be imported cleanly, it will bootstrap and
reuse a local `uart/tools/.venv-ml` virtual environment automatically.
"""

from __future__ import annotations

import argparse
import csv
import os
import subprocess
import sys
from typing import TYPE_CHECKING, Iterable

from tool_config import (
    DEFAULT_DATASET_PATH,
    DEFAULT_FIRMWARE_MODEL_PATH,
    DEFAULT_GENERATED_MODEL_PATH,
    FEATURE_NAMES,
    GESTURE_LABELS,
)

if TYPE_CHECKING:
    import numpy as np


DEFAULT_DATA = DEFAULT_DATASET_PATH
DEFAULT_OUT = DEFAULT_GENERATED_MODEL_PATH
DEFAULT_FIRMWARE_OUT = DEFAULT_FIRMWARE_MODEL_PATH

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
LOCAL_VENV_DIR = os.path.join(TOOLS_DIR, ".venv-ml")
TRAIN_REQUIREMENTS = os.path.join(TOOLS_DIR, "requirements-train.txt")
VENV_ACTIVE_ENV = "UART_TOOLS_VENV_ACTIVE"


def get_venv_python(venv_dir: str) -> str:
    """Return the Python executable inside the local training virtualenv."""
    if os.name == "nt":
        return os.path.join(venv_dir, "Scripts", "python.exe")
    return os.path.join(venv_dir, "bin", "python")


def probe_training_dependencies(python_executable: str) -> tuple[bool, str]:
    """Check whether numpy and scikit-learn import cleanly in a given Python."""
    result = subprocess.run(
        [python_executable, "-c", "import numpy, sklearn"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if result.returncode == 0:
        return True, ""

    detail = (result.stderr or result.stdout or "").strip()
    if detail:
        lines = [line.strip() for line in detail.splitlines() if line.strip()]
        return False, lines[-1]
    return False, f"dependency probe exited with code {result.returncode}"


def install_training_dependencies(venv_python: str) -> None:
    """Install the pinned training dependencies into the local virtualenv."""
    if not os.path.exists(TRAIN_REQUIREMENTS):
        print(f"[错误] 找不到训练依赖文件: {TRAIN_REQUIREMENTS}")
        raise SystemExit(1)

    print("[环境] 正在安装训练依赖，请稍候 ...", flush=True)
    subprocess.check_call(
        [
            venv_python,
            "-m",
            "pip",
            "install",
            "--disable-pip-version-check",
            "-r",
            TRAIN_REQUIREMENTS,
        ]
    )


def ensure_training_runtime() -> None:
    """
    Ensure the current process has a usable training runtime.

    If the active interpreter is broken, create / repair `uart/tools/.venv-ml`
    and re-run the script inside that environment.
    """
    ok, reason = probe_training_dependencies(sys.executable)
    if ok:
        return

    venv_python = get_venv_python(LOCAL_VENV_DIR)
    already_reexecuted = os.environ.get(VENV_ACTIVE_ENV) == "1"

    if already_reexecuted:
        print("[错误] 本地训练环境中的依赖仍不可用。", flush=True)
        print(f"       当前解释器: {sys.executable}", flush=True)
        print(f"       失败原因: {reason}", flush=True)
        print(f"       可尝试删除后重建目录: {LOCAL_VENV_DIR}", flush=True)
        raise SystemExit(1)

    if not os.path.exists(venv_python):
        print("[环境] 检测到当前 Python 训练依赖不可用。", flush=True)
        print(f"       当前解释器: {sys.executable}", flush=True)
        print(f"       失败原因: {reason}", flush=True)
        print(f"[环境] 正在创建独立训练环境: {LOCAL_VENV_DIR}", flush=True)
        subprocess.check_call([sys.executable, "-m", "venv", LOCAL_VENV_DIR])
    else:
        print("[环境] 当前 Python 训练依赖不可用，将切换到本地训练环境。", flush=True)
        print(f"       当前解释器: {sys.executable}", flush=True)
        print(f"       失败原因: {reason}", flush=True)

    venv_ok, _ = probe_training_dependencies(venv_python)
    if not venv_ok:
        install_training_dependencies(venv_python)
        venv_ok, venv_reason = probe_training_dependencies(venv_python)
        if not venv_ok:
            print("[错误] 本地训练环境创建完成，但依赖仍无法导入。", flush=True)
            print(f"       解释器: {venv_python}", flush=True)
            print(f"       失败原因: {venv_reason}", flush=True)
            raise SystemExit(1)

    print(f"[环境] 使用本地训练解释器: {venv_python}", flush=True)
    env = os.environ.copy()
    env[VENV_ACTIVE_ENV] = "1"
    completed = subprocess.run([venv_python, os.path.abspath(__file__), *sys.argv[1:]], env=env, check=False)
    raise SystemExit(completed.returncode)


def load_dataset(csv_path: str) -> tuple["np.ndarray", "np.ndarray"]:
    """
    Load the dataset CSV and validate that labels cover the full 0~9 range.

    The loader supports both CSV files with a header row and legacy files
    without headers.
    """
    if not os.path.exists(csv_path):
        print(f"[错误] 找不到数据文件: {csv_path}")
        raise SystemExit(1)

    with open(csv_path, "r", encoding="utf-8-sig", newline="") as csv_file:
        rows = list(csv.reader(csv_file))

    if not rows:
        print("[错误] 数据文件为空，无法训练。")
        raise SystemExit(1)

    expected_columns = FEATURE_NAMES + ["label"]
    first_row = [cell.strip() for cell in rows[0]]
    has_header = set(expected_columns).issubset(first_row)

    if has_header:
        try:
            column_indexes = [first_row.index(name) for name in expected_columns]
        except ValueError:
            print("[错误] CSV 表头缺少必要字段。")
            raise SystemExit(1)
        data_rows = rows[1:]
        data_row_start = 2
    else:
        if len(first_row) != len(expected_columns):
            print("[错误] CSV 列数不正确。")
            print(f"       期望 {len(expected_columns)} 列: 10 个特征 + 1 个 label")
            raise SystemExit(1)
        column_indexes = list(range(len(expected_columns)))
        data_rows = rows
        data_row_start = 1

    feature_rows: list[list[float]] = []
    labels: list[int] = []

    for row_number, row in enumerate(data_rows, start=data_row_start):
        if not row or all(not cell.strip() for cell in row):
            continue

        if max(column_indexes) >= len(row):
            print(f"[错误] CSV 第 {row_number} 行列数不足。")
            raise SystemExit(1)

        selected = [row[index].strip() for index in column_indexes]
        if any(value == "" for value in selected):
            continue

        try:
            features = [float(value) for value in selected[:-1]]
            label = int(selected[-1])
        except ValueError:
            print(f"[错误] CSV 第 {row_number} 行存在无法解析的数值。")
            raise SystemExit(1)

        feature_rows.append(features)
        labels.append(label)

    if not feature_rows:
        print("[错误] 数据文件中没有可用样本，无法训练。")
        raise SystemExit(1)

    X = np.asarray(feature_rows, dtype=np.float32)
    y = np.asarray(labels, dtype=np.int32)

    expected_labels = list(GESTURE_LABELS.keys())
    actual_labels = sorted(set(y.tolist()))
    if actual_labels != expected_labels:
        print("[错误] 当前工程要求 10 类完整标签 0~9。")
        print(f"       实际检测到的标签: {actual_labels}")
        print("       请确认 CSV 中已经补齐 three(label=9) 等缺失类别后再训练。")
        raise SystemExit(1)

    print(f"[数据] 共 {len(X)} 帧，特征维度 {len(FEATURE_NAMES)}。")
    for label in expected_labels:
        count = int((y == label).sum())
        print(f"  [{label}] {GESTURE_LABELS[label]:>5s}: {count:4d} 帧")

    return X, y


def choose_n_splits(y: "np.ndarray") -> int:
    """Pick a valid cross-validation split count from the smallest class size."""
    counts = [int((y == label).sum()) for label in GESTURE_LABELS]
    min_count = min(counts)
    if min_count < 2:
        print("[错误] 每一类至少需要 2 帧数据，当前最小类别不足。")
        raise SystemExit(1)
    return min(5, min_count)


def train_model(X: "np.ndarray", y: "np.ndarray"):
    """Train a linear SVM with standardization and print CV accuracy."""
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
    """Format a 1D float array as a C static constant definition."""
    values_list = list(values)
    content = ", ".join(f"{value:.6f}f" for value in values_list)
    return f"static const float {name}[{len(values_list)}] = {{{content}}};"


def format_c_2d(values: "np.ndarray", name: str) -> str:
    """Format a 2D float array as a C static constant definition."""
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
    """Write the generated model header text to disk."""
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
    """Export the StandardScaler + LinearSVC weights to C header files."""
    mean_ = scaler.mean_.astype(np.float32)
    scale_ = scaler.scale_.astype(np.float32)
    coef_ = svm.coef_.astype(np.float32)
    bias_ = svm.intercept_.astype(np.float32)

    lines = [
        "/**",
        " * gesture_model.h",
        " * ------------------------------------------------------------",
        " * Auto-generated by uart/tools/train_svm.py",
        f" * Mean cross-validation accuracy: {accuracy * 100:.2f}%",
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
        "/* StandardScaler parameters */",
        format_c_1d(mean_.tolist(), "gm_mean"),
        format_c_1d(scale_.tolist(), "gm_scale"),
        "",
        "/* LinearSVC weights and bias */",
        format_c_2d(coef_, "gm_W"),
        format_c_1d(bias_.tolist(), "gm_b"),
        "",
        "/* Label names for debugging */",
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
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description="训练线性 SVM 并导出 C 权重头文件")
    parser.add_argument("--data", default=DEFAULT_DATA, help="输入 CSV 文件路径")
    parser.add_argument("--out", default=DEFAULT_OUT, help="输出头文件路径")
    parser.add_argument(
        "--firmware-out",
        default=DEFAULT_FIRMWARE_OUT,
        help="同步写入固件工程使用的模型头文件路径；传空字符串则只生成 --out",
    )
    return parser.parse_args()


def main() -> None:
    """Main entry point."""
    ensure_training_runtime()

    global np
    import numpy as np

    args = parse_args()
    firmware_out = args.firmware_out.strip()

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
