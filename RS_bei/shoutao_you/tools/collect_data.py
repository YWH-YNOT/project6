"""
collect_data.py — 手势数据统一采集工具 v5
==========================================
9个类别全部写入同一个 gesture_data.csv，
label 0-4 原有手势，label 5-8 方向手势。

快捷命令：
  G0 握拳(fist)    G1 张开(open)   G2 比1(one)
  G3 比2/V(two)    G4 摇滚(rock)
  U/UP  ↑ 上(up,label5)    D/DOWN  ↓ 下(down,label6)
  L/LEFT ← 左(left,label7) R/RIGHT → 右(right,label8)

  OVER           停止本次采集
  status         查看所有数量
  DELETE <n>     删除标签 n 的全部数据（0~8）
  quit / q       保存并退出
"""

import serial
import csv
import sys
import os
import threading
import time

# ============================================================
PORT             = "COM7"
BAUD             = 115200
GESTURE_FILE     = "gesture_data.csv"
N_FEATURES       = 10
DISPLAY_INTERVAL = 20
# ============================================================

FEATURE_NAMES = ["f0x","f0y","f1x","f1y","f2x","f2y","f3x","f3y","roll","pitch"]
COLUMNS       = FEATURE_NAMES + ["label"]

# 9类标签表
ALL_NAMES = {
    0: "握拳(fist)",
    1: "张开(open)",
    2: "比1(one)",
    3: "比2/V(two)",
    4: "摇滚(rock)",
    5: "↑ 上(up)",
    6: "↓ 下(down)",
    7: "← 左(left)",
    8: "→ 右(right)",
}

GESTURE_TARGET = 200   # 每类建议采集帧数

# 全局状态
collecting    = False
current_label = -1
running       = True
lock          = threading.Lock()
frame_counter = 0

count = {k: 0 for k in ALL_NAMES}

# CSV IO
_io = {}   # {"file":..., "writer":...}

# 诊断
_diag_lock   = threading.Lock()
_last_raw    = ""
_last_ncols  = 0
_total_lines = 0


# ─────────────────────────────────────────────────────────────────────────────
# 串口读取线程
# ─────────────────────────────────────────────────────────────────────────────
def serial_reader(ser):
    global frame_counter, _last_raw, _last_ncols, _total_lines
    warn_printed = False
    while running:
        try:
            raw = ser.readline().decode("utf-8", errors="ignore").strip()
        except Exception:
            continue
        if not raw:
            continue

        parts = raw.split(",")

        with _diag_lock:
            _last_raw    = raw
            _last_ncols  = len(parts)
            _total_lines += 1

        if len(parts) != N_FEATURES:
            if not warn_printed:
                print(f"\n  [警告] 串口收到 {len(parts)} 列，期望 {N_FEATURES} 列")
                print(f"  示例：{raw[:60]}")
                if len(parts) == 4:
                    print("  → STM32 疑似在挥手采集模式（4列），请切换为10列输出")
                warn_printed = True
            continue

        warn_printed = False

        try:
            values = [float(p) for p in parts]
        except ValueError:
            continue

        with lock:
            if collecting and current_label >= 0:
                entry = _io.get("ges")
                if entry:
                    entry["writer"].writerow(values + [current_label])
                    entry["file"].flush()
                count[current_label] += 1
                frame_counter += 1
                if frame_counter % DISPLAY_INTERVAL == 0:
                    _print_live(values)


def _print_live(values):
    line = "  [实时] " + "  ".join(f"{n}:{v:7.2f}" for n, v in zip(FEATURE_NAMES, values))
    print(line)
    print("  >>> ", end="", flush=True)


# ─────────────────────────────────────────────────────────────────────────────
# 状态显示
# ─────────────────────────────────────────────────────────────────────────────
def show_status():
    print()
    print("  ┌── 所有手势数据（gesture_data.csv）─────────────────")
    for k, name in ALL_NAMES.items():
        cnt = count.get(k, 0)
        bar = "█" * min(cnt // 10, 20)
        tip = "✓" if cnt >= GESTURE_TARGET else f"差{max(0,GESTURE_TARGET-cnt)}帧"
        print(f"  │  [{k}] {name:16s} {cnt:4d}帧  {bar}  {tip}")
    print(f"  └  合计: {sum(count.values())} 帧")
    print()


# ─────────────────────────────────────────────────────────────────────────────
# 删除数据
# ─────────────────────────────────────────────────────────────────────────────
def delete_rows(label: int):
    if not os.path.exists(GESTURE_FILE):
        print(f"  [提示] 文件 {GESTURE_FILE} 不存在")
        return
    with open(GESTURE_FILE, "r", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    if not rows:
        print("  [提示] 文件为空")
        return
    header     = rows[0]
    data       = rows[1:]
    label_col  = header.index("label") if "label" in header else -1
    if label_col < 0:
        print("  [错误] 找不到 label 列")
        return
    before  = len(data)
    data    = [r for r in data if r[label_col] != str(label)]
    deleted = before - len(data)
    with open(GESTURE_FILE, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(header)
        writer.writerows(data)
    count[label] = 0
    print(f"  [删除] 已删除 {deleted} 帧（label={label}: {ALL_NAMES.get(label,'?')}）")
    show_status()


# ─────────────────────────────────────────────────────────────────────────────
# 帮助
# ─────────────────────────────────────────────────────────────────────────────
def print_help():
    print("=" * 60)
    print("  手势数据统一采集工具 v5  （单CSV · 9类）")
    print("=" * 60)
    print()
    print("  原有手势（label 0-4）:")
    for k in range(5):
        print(f"    G{k}  → {ALL_NAMES[k]}")
    print()
    print("  方向手势（label 5-8）:")
    for k, cmd in [("U","↑ 上"),("D","↓ 下"),("L","← 左"),("R","→ 右")]:
        lbl = {"U":5,"D":6,"L":7,"R":8}[k]
        print(f"    {k}   → {ALL_NAMES[lbl]}")
    print()
    print("  通用命令:")
    print("    OVER           停止本次采集")
    print("    status         查看所有数量")
    print("    DELETE <n>     删除标签n的数据（0~8）")
    print("    quit / q       保存并退出")
    print()
    print(f"  建议每类 ≥ {GESTURE_TARGET} 帧")
    print("=" * 60)
    print()


# ─────────────────────────────────────────────────────────────────────────────
# CSV 初始化
# ─────────────────────────────────────────────────────────────────────────────
def _load_existing_counts():
    if not os.path.exists(GESTURE_FILE):
        return
    with open(GESTURE_FILE, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if "label" not in (reader.fieldnames or []):
            return
        for row in reader:
            try:
                lbl = int(row["label"])
                if lbl in count:
                    count[lbl] += 1
            except Exception:
                pass


def _open_csv():
    need_header = not os.path.exists(GESTURE_FILE)
    f = open(GESTURE_FILE, "a", newline="", encoding="utf-8")
    w = csv.writer(f)
    if need_header:
        w.writerow(COLUMNS)
    return f, w


# ─────────────────────────────────────────────────────────────────────────────
# do_start
# ─────────────────────────────────────────────────────────────────────────────
def do_start(label: int):
    global collecting, current_label, frame_counter
    name = ALL_NAMES[label]
    with lock:
        current_label = label
        collecting    = True
        frame_counter = 0
    print(f"  [采集中] [{label}] {name}  →  保持手势不动，每 {DISPLAY_INTERVAL} 帧刷新，输入 OVER 停止")


# ─────────────────────────────────────────────────────────────────────────────
# 主函数
# ─────────────────────────────────────────────────────────────────────────────
def main():
    global running, collecting, _io

    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
        print(f"[OK] 已连接串口 {PORT}，波特率 {BAUD}")
    except Exception as e:
        print(f"[错误] 无法打开串口 {PORT}：{e}")
        sys.exit(1)

    _load_existing_counts()

    g_csv, g_writer = _open_csv()
    _io = {"ges": {"file": g_csv, "writer": g_writer}}

    t = threading.Thread(target=serial_reader, args=(ser,), daemon=True)
    t.start()

    # 启动诊断 2 秒
    print("[等待] 串口预热 2 秒...")
    time.sleep(2)

    with _diag_lock:
        lines  = _total_lines
        ncols  = _last_ncols
        sample = _last_raw

    if lines == 0:
        print(f"[警告] ⚠️  未收到任何串口数据！请检查串口号（PORT={PORT}）或 STM32 状态")
    elif ncols != N_FEATURES:
        print(f"[警告] ⚠️  收到 {ncols} 列，需要 {N_FEATURES} 列  示例：{sample[:60]}")
    else:
        print(f"[就绪] ✓ {lines} 行 × {ncols} 列，可以开始采集\n")

    print_help()
    show_status()

    DIR_MAP = {"U": 5, "UP": 5, "D": 6, "DOWN": 6, "L": 7, "LEFT": 7, "R": 8, "RIGHT": 8}

    try:
        while running:
            try:
                cmd = input(">>> ").strip()
            except (EOFError, KeyboardInterrupt):
                break
            if not cmd:
                continue

            parts = cmd.split()
            op    = parts[0].upper()

            # G0~G8 快捷键
            if len(op) == 2 and op[0] == "G":
                try:
                    label = int(op[1])
                except ValueError:
                    print(f"  [错误] 无效标签：{op}")
                    continue
                if label not in ALL_NAMES:
                    print(f"  [错误] 标签范围 0~{max(ALL_NAMES.keys())}")
                    continue
                do_start(label)

            # 方向快捷键
            elif op in DIR_MAP:
                do_start(DIR_MAP[op])

            # OVER
            elif op == "OVER":
                with lock:
                    collecting = False
                cnt  = count.get(current_label, 0)
                name = ALL_NAMES.get(current_label, "?")
                print(f"  [停止] [{current_label}] {name}  已采集 {cnt} 帧")
                show_status()

            # DELETE <n>
            elif op == "DELETE":
                if len(parts) < 2:
                    print("  [提示] 用法：DELETE <0~8>")
                    continue
                try:
                    label = int(parts[1])
                except ValueError:
                    print("  [错误] 编号须为整数")
                    continue
                if label not in ALL_NAMES:
                    print(f"  [错误] 标签范围 0~{max(ALL_NAMES.keys())}")
                    continue
                confirm = input(f"  确认删除 [{label}]（{ALL_NAMES[label]}）的全部数据？(y/n) ").strip().lower()
                if confirm == "y":
                    with lock:
                        if collecting and current_label == label:
                            collecting = False
                    delete_rows(label)

            # STATUS
            elif op == "STATUS":
                show_status()

            # QUIT
            elif op in ("QUIT", "Q"):
                break

            else:
                print(f"  [未知命令] {cmd}")
                print("  提示：G0~G4 原有手势，U/D/L/R 方向手势，OVER 停止，DELETE <n> 删除")

    finally:
        running = False
        with lock:
            collecting = False
        try:
            _io["ges"]["file"].close()
        except Exception:
            pass
        ser.close()
        print("\n[退出] 数据已保存到", GESTURE_FILE)
        show_status()


if __name__ == "__main__":
    main()
