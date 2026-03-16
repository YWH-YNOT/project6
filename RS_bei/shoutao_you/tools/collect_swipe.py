"""
collect_swipe.py — STM32 挥手方向数据采集工具 v3
================================================
操作方式：【先按键 → 倒计时 → 挥手 → 自动停止 → 确认保存】

自动停止逻辑：
  采集开始后监测 gyro 强度（gyro_z²+gyro_y²），
  当检测到动作开始后 gyro 重新归零，自动结束采集。
  过滤掉前后静止帧，只保留有效运动段，再重采样到 WINDOW_SIZE 帧。

STM32 串口格式：roll,pitch,gyro_z,gyro_y（4维）

命令：
  L/R/U/D  (或 LEFT/RIGHT/UP/DOWN) — 开始采集对应方向
  status   — 查看各方向数量
  DELETE <id> — 删除某方向全部数据
  quit/q   — 退出保存
"""

import serial, csv, sys, os, threading, time, collections
import numpy as np

# ============================================================
PORT        = "COM7"
BAUD        = 115200
OUTPUT_FILE = "swipe_data.csv"
N_CHANNELS  = 4          # roll, pitch, gyro_z, gyro_y
WINDOW_SIZE = 40         # 最终保存的帧数（重采样到此长度）

# ---- 动作检测参数 ----
READY_DELAY      = 1.0   # 按键后等待时间（s）
MOTION_START_TH  = 30.0  # gyro 强度超过此值认为动作开始（°/s 合成）
MOTION_END_TH    = 15.0  # gyro 强度低于此值认为动作结束（连续 END_FRAMES 帧）
MOTION_END_FRAMES= 8     # 连续低强度帧数才确认结束
MAX_CAPTURE_SEC  = 3.0   # 最长等待时间（s），超时自动结束
MIN_ACTIVE_FRAMES= 8     # 有效帧数下限，不足则提示重采
# ============================================================

SWIPE_NAMES   = {0: "左挥(left)", 1: "右挥(right)", 2: "上挥(up)", 3: "下挥(down)"}
CHANNEL_NAMES = ["roll", "pitch", "gyro_z", "gyro_y"]

# 全局
running      = True
frame_queue  = collections.deque()   # 无限队列，串口线程追加
queue_lock   = threading.Lock()
sample_count = {k: 0 for k in SWIPE_NAMES}
latest_frame = [0.0] * N_CHANNELS


def gyro_strength(frame):
    """计算 gyro_z²+gyro_y² 的平方根（合成角速度）"""
    gz, gy = frame[2], frame[3]
    return (gz * gz + gy * gy) ** 0.5


def serial_reader(ser):
    global running
    while running:
        try:
            raw = ser.readline().decode("utf-8", errors="ignore").strip()
        except Exception:
            continue
        if not raw:
            continue
        parts = raw.split(",")
        if len(parts) != N_CHANNELS:
            continue
        try:
            values = [float(p) for p in parts]
        except ValueError:
            continue
        with queue_lock:
            frame_queue.append((time.monotonic(), values))
            for i, v in enumerate(values):
                latest_frame[i] = v


def collect_adaptive() -> list:
    """
    自适应采集：
      1. 清空队列，等待 gyro 强度超过 MOTION_START_TH（动作开始）
      2. 继续采集，直到 gyro 连续 MOTION_END_FRAMES 帧 < MOTION_END_TH（动作结束）
      3. 返回所有有效帧（已过滤两端静止帧）
    超过 MAX_CAPTURE_SEC 秒自动结束。
    """
    # 清空旧数据
    with queue_lock:
        frame_queue.clear()

    start_t = time.monotonic()
    deadline = start_t + MAX_CAPTURE_SEC

    all_frames   = []   # 采集到的所有原始帧
    motion_start = False
    low_cnt      = 0    # 连续低强度帧计数

    # --- 动态进度条 ---
    bar_chars = 40

    while time.monotonic() < deadline:
        time.sleep(0.005)

        with queue_lock:
            new_frames = [(t, v) for t, v in frame_queue if t >= start_t]
            frame_queue.clear()

        for t, v in new_frames:
            all_frames.append(v)
            gs = gyro_strength(v)

            if not motion_start:
                if gs >= MOTION_START_TH:
                    motion_start = True
                    low_cnt = 0
            else:
                if gs < MOTION_END_TH:
                    low_cnt += 1
                    if low_cnt >= MOTION_END_FRAMES:
                        # 动作结束，去掉尾部低强度帧
                        active = all_frames[: len(all_frames) - low_cnt + 1]
                        print(f"\r  ✓ 动作结束，采集 {len(all_frames)} 帧，"
                              f"有效 {len(active)} 帧                    ")
                        return active
                else:
                    low_cnt = 0

        # 进度条
        elapsed = time.monotonic() - start_t
        n_bars  = int(elapsed / MAX_CAPTURE_SEC * bar_chars)
        status  = "等待动作..." if not motion_start else "采集中..."
        print(f"\r  [{('█' * n_bars).ljust(bar_chars)}] {status}  ", end="", flush=True)

    print(f"\r  超时，共采集 {len(all_frames)} 帧                              ")

    # 过滤两端静止帧
    if not all_frames:
        return []

    # 找首尾有效帧（gyro 超过 MOTION_END_TH）
    start_idx = next((i for i, f in enumerate(all_frames)
                      if gyro_strength(f) >= MOTION_END_TH), 0)
    end_idx   = next((i for i, f in enumerate(reversed(all_frames))
                      if gyro_strength(f) >= MOTION_END_TH), 0)
    end_idx   = len(all_frames) - end_idx

    active = all_frames[start_idx:end_idx]
    print(f"  有效帧：{len(active)} 帧（索引 {start_idx}~{end_idx}）")
    return active


def resample_to_window(frames: list, target: int) -> list:
    """
    将变长帧序列（numpy-style）重采样（线性插值）到 target 帧。
    frames: [[roll,pitch,gz,gy], ...]
    """
    n = len(frames)
    if n == target:
        return frames
    arr = np.array(frames, dtype=np.float32)          # (n, 4)
    src_x = np.linspace(0, 1, n)
    dst_x = np.linspace(0, 1, target)
    result = np.stack([np.interp(dst_x, src_x, arr[:, ch])
                       for ch in range(N_CHANNELS)], axis=1)
    return result.tolist()


def preview(frames: list, label: int) -> str:
    name = SWIPE_NAMES[label]
    rolls   = [f[0] for f in frames]
    pitchs  = [f[1] for f in frames]
    gz_vals = [f[2] for f in frames]
    gy_vals = [f[3] for f in frames]

    def stat(arr):
        pk = max(arr, key=abs)
        mn = sum(arr) / len(arr)
        return f"均值={mn:+6.1f}  峰值={pk:+7.1f}"

    return (f"  [{label}] {name}  ({len(frames)} 帧)\n"
            f"  roll  : {stat(rolls)}°\n"
            f"  pitch : {stat(pitchs)}°\n"
            f"  gyro_z: {stat(gz_vals)}°/s\n"
            f"  gyro_y: {stat(gy_vals)}°/s")


def save_window(frames: list, label: int, writer, csv_file):
    flat = [v for frame in frames for v in frame]
    writer.writerow(flat + [label])
    csv_file.flush()
    with queue_lock:
        sample_count[label] = sample_count.get(label, 0) + 1


def do_collect(label: int, writer, csv_file):
    name = SWIPE_NAMES[label]

    while True:
        # 倒计时
        print(f"\n[准备] 即将采集「{name}」— 倒计时")
        for i in range(int(READY_DELAY * 10), 0, -1):
            print(f"\r  {i/10:.1f}s 后开始 ...", end="", flush=True)
            time.sleep(0.1)
        print(f"\r  \033[1;32m>>> 现在挥手！<<<\033[0m            ")

        # 自适应采集
        active_frames = collect_adaptive()

        if len(active_frames) < MIN_ACTIVE_FRAMES:
            print(f"\n  [提示] 有效帧只有 {len(active_frames)} 帧（需 ≥ {MIN_ACTIVE_FRAMES}），"
                  f"gyro 强度是否过低？")
            print(f"  动作开始阈值={MOTION_START_TH}°/s，可在脚本顶部调整 MOTION_START_TH")
            ans = input("  重试？(y=重采  n=放弃) > ").strip().lower()
            if ans != "y":
                return
            continue

        # 重采样到 WINDOW_SIZE
        resampled = resample_to_window(active_frames, WINDOW_SIZE)

        # 预览
        print(f"\n[预览]")
        print(preview(active_frames, label))
        print(f"  → 已重采样至 {WINDOW_SIZE} 帧（用于 SVM）")

        try:
            ans = input("\n  保存？(y=保存  n=重采  q=放弃) > ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            return

        if ans == "y":
            save_window(resampled, label, writer, csv_file)
            print(f"  ✓ 已保存  [{label}] {name}  共 {sample_count[label]} 个")
            return
        elif ans == "n":
            print("  [重采] 重新开始...")
        else:
            print("  [放弃]")
            return


def show_status():
    print("\n  各方向已采集样本数：")
    for k, name in SWIPE_NAMES.items():
        cnt = sample_count.get(k, 0)
        bar = "█" * min(cnt // 5, 20)
        print(f"    [{k}] {name:15s}  {cnt:4d} 个  {bar}")
    print(f"    合计: {sum(sample_count.values())} 个")


def delete_label(label: int):
    if not os.path.exists(OUTPUT_FILE):
        print("  [提示] 文件不存在")
        return
    with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    before = len(rows)
    data   = [r for r in rows if not r or r[-1] != str(label)]
    with open(OUTPUT_FILE, "w", newline="", encoding="utf-8") as f:
        csv.writer(f).writerows(data)
    sample_count[label] = 0
    print(f"  [删除] 已删除 {before - len(data)} 个 [{label}]{SWIPE_NAMES[label]} 样本")
    show_status()


CMD_MAP = {"L": 0, "LEFT": 0, "R": 1, "RIGHT": 1, "U": 2, "UP": 2, "D": 3, "DOWN": 3}


def main():
    global running

    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
        print(f"[OK] 串口 {PORT} @ {BAUD}")
    except Exception as e:
        print(f"[错误] 无法打开串口：{e}")
        sys.exit(1)

    # 统计现有数据
    if os.path.exists(OUTPUT_FILE):
        with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
            for row in csv.reader(f):
                if not row:
                    continue
                try:
                    lbl = int(row[-1])
                    if lbl in sample_count:
                        sample_count[lbl] += 1
                except Exception:
                    pass
        if sum(sample_count.values()) > 0:
            print("[信息] 已有数据：" +
                  "  ".join(f"[{k}]{v}个" for k, v in sample_count.items() if v > 0))

    csv_file = open(OUTPUT_FILE, "a", newline="", encoding="utf-8")
    writer   = csv.writer(csv_file)

    t = threading.Thread(target=serial_reader, args=(ser,), daemon=True)
    t.start()

    print("\n" + "=" * 60)
    print("  挥手采集 v3  |  自动停止 + 动作检测")
    print("=" * 60)
    print("  L/R/U/D    开始采集对应方向（倒计时后挥手）")
    print("  status     查看各方向数量（目标每向 ≥ 50 个）")
    print("  DELETE <n> 删除某方向全部数据")
    print("  quit/q     退出")
    print("=" * 60)
    print(f"\n  动作阈值：开始>{MOTION_START_TH}°/s  结束<{MOTION_END_TH}°/s  "
          f"最长{MAX_CAPTURE_SEC}s")
    print("  如需调整灵敏度，修改脚本顶部的 MOTION_START_TH / MOTION_END_TH\n")

    print("[等待] 串口预热 2 秒...")
    time.sleep(2)
    with queue_lock:
        n = len(frame_queue)
    if n == 0:
        print("[警告] 未收到串口数据，请检查 PORT 和 STM32 输出")
    else:
        print(f"[就绪] 串口正常（{n}帧）\n")

    try:
        while running:
            try:
                cmd = input(">>> ").strip().upper()
            except (EOFError, KeyboardInterrupt):
                break
            if not cmd:
                continue
            parts = cmd.split()
            op = parts[0]

            if op in CMD_MAP:
                do_collect(CMD_MAP[op], writer, csv_file)
            elif op == "DELETE":
                if len(parts) < 2:
                    print("  用法：DELETE <0~3>")
                    continue
                try:
                    lbl = int(parts[1])
                except ValueError:
                    print("  id 须为整数")
                    continue
                if lbl not in SWIPE_NAMES:
                    print("  id 范围 0~3")
                    continue
                ans = input(f"  确认删除 [{lbl}]{SWIPE_NAMES[lbl]}？(y/n) ").strip().lower()
                if ans == "y":
                    delete_label(lbl)
            elif op == "STATUS":
                show_status()
            elif op in ("QUIT", "Q"):
                break
            else:
                print(f"  未知命令：{cmd}")
    finally:
        running = False
        csv_file.close()
        ser.close()
        print("\n[退出] 已保存至", OUTPUT_FILE)
        show_status()


if __name__ == "__main__":
    main()
