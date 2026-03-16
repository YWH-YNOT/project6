"""
test_gesture.py — PC端手势实时识别测试
=========================================
不需要重新烧录 STM32！

用法：
  python test_gesture.py

功能：
  1. 从 gesture_data.csv 重新训练SVM模型（约1秒）
  2. 读取STM32串口数据
  3. 实时滑动窗口投票，显示识别结果
  4. 打印混淆矩阵（Ctrl+C）退出时显示

输出示例：
  [实时] f0x:-12.34  f1x:-8.23 ... → 手势: 比1(one)  ✓ 置信度: 82%
"""

import serial
import sys
import os
import time
import threading
import collections

# ============================================================
PORT        = "COM7"
BAUD        = 115200
DATA_CSV    = "gesture_data.csv"
VOTE_WINDOW = 10    # 投票窗口帧数（越大越稳定，响应略慢）
# ============================================================

GESTURE_NAMES = {
    0: "握拳(fist)",
    1: "张开(open)",
    2: "比1(one)",
    3: "比2/V(two)",
    4: "比1+小指(rock)",
}

# ============= 颜色输出 =============
def green(s):  return f"\033[92m{s}\033[0m"
def yellow(s): return f"\033[93m{s}\033[0m"
def red(s):    return f"\033[91m{s}\033[0m"
def bold(s):   return f"\033[1m{s}\033[0m"


def train_model(csv_path):
    """从CSV训练SVM，返回 (predict函数, feature_cols, n_features)"""
    try:
        import numpy as np
        import pandas as pd
        from sklearn.svm import LinearSVC
        from sklearn.preprocessing import StandardScaler
        from sklearn.pipeline import Pipeline
    except ImportError:
        print("[错误] 请安装依赖：pip install scikit-learn numpy pandas")
        sys.exit(1)

    if not os.path.exists(csv_path):
        print(f"[错误] 找不到 {csv_path}，请先运行 collect_data.py 采集数据")
        sys.exit(1)

    # 自动识别CSV格式（复用与 train_svm.py 相同逻辑）
    df_peek = pd.read_csv(csv_path, nrows=1)
    first_col = df_peek.columns[0]
    has_header = True
    try:
        float(first_col)
        has_header = False
    except ValueError:
        pass

    if has_header:
        df = pd.read_csv(csv_path)
    else:
        df = pd.read_csv(csv_path, header=None)
        n = len(df.columns)
        if n == 7:
            df.columns = ["f0","f1","f2","f3","roll","pitch","label"]
        elif n == 11:
            df.columns = ["f0x","f0y","f1x","f1y","f2x","f2y","f3x","f3y","roll","pitch","label"]
        elif n == 13:
            df.columns = ["f0x","f0y","f1x","f1y","f2x","f2y","f3x","f3y","f4x","f4y","roll","pitch","label"]
        else:
            print(f"[错误] 不支持的列数 {n}")
            sys.exit(1)

    cols = df.columns.tolist()
    if "f0x" in cols and "f4x" in cols:
        FCOLS = ["f0x","f0y","f1x","f1y","f2x","f2y","f3x","f3y","f4x","f4y","roll","pitch"]
    elif "f0x" in cols:
        FCOLS = ["f0x","f0y","f1x","f1y","f2x","f2y","f3x","f3y","roll","pitch"]
    else:
        FCOLS = ["f0","f1","f2","f3","roll","pitch"]

    df = df.dropna()
    X = df[FCOLS].values.astype("float32")
    y = df["label"].values.astype(int)
    n_features = len(FCOLS)

    pipe = Pipeline([
        ("scaler", StandardScaler()),
        ("svm",    LinearSVC(C=1.0, max_iter=5000, random_state=42)),
    ])
    pipe.fit(X, y)
    print(f"[训练] 完成，{len(X)} 帧，{n_features} 维特征，{len(set(y))} 类")
    return pipe, FCOLS, n_features


def main():
    # ——— 训练模型 ———
    print("=" * 55)
    print("  手势识别实时测试工具")
    print("=" * 55)
    pipe, FCOLS, n_features = train_model(DATA_CSV)

    # ——— 连接串口 ———
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
        print(f"[OK] 串口 {PORT} 已连接")
    except Exception as e:
        print(f"[错误] {e}")
        sys.exit(1)

    # 投票窗口
    vote_buf = collections.deque(maxlen=VOTE_WINDOW)
    stats    = {k: 0 for k in GESTURE_NAMES}   # 统计各手势识别次数
    last_gesture = -1

    print(f"\n[开始] 实时识别中（投票窗口={VOTE_WINDOW}帧）... Ctrl+C 退出\n")
    print(f"{'传感器数据':<60}  识别结果")
    print("-" * 80)

    try:
        while True:
            try:
                raw = ser.readline().decode("utf-8", errors="ignore").strip()
            except Exception:
                continue

            if not raw or raw.startswith("#"):
                continue

            parts = raw.split(",")
            if len(parts) != n_features:
                continue
            try:
                values = [float(p) for p in parts]
            except ValueError:
                continue

            # 推理
            import numpy as np
            pred = int(pipe.predict([values])[0])
            vote_buf.append(pred)

            # 投票取众数
            counts = collections.Counter(vote_buf)
            winner, win_cnt = counts.most_common(1)[0]
            confidence = int(win_cnt / len(vote_buf) * 100)

            # 只在手势变化或每50帧刷新一次时打印
            feature_str = "  ".join(f"{n}:{v:6.1f}" for n, v in zip(FCOLS[:4], values[:4]))
            gesture_str = GESTURE_NAMES.get(winner, "?")

            if winner != last_gesture:
                if winner == 0:
                    color = red
                elif winner == 1:
                    color = yellow
                else:
                    color = green
                print(f"{feature_str:<58}  {color(bold(gesture_str))}  {confidence}%")
                last_gesture = winner
                stats[winner] = stats.get(winner, 0) + 1

    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        print("\n\n[统计] 本次识别各手势触发次数：")
        for k, cnt in stats.items():
            if cnt > 0:
                print(f"  [{k}] {GESTURE_NAMES[k]:20s}: {cnt} 次")


if __name__ == "__main__":
    main()
