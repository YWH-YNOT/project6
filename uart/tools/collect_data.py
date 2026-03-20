"""
collect_data.py
================
从 RA6M5 的 `uart7` 采集 10 维手势特征，并在 PC 本地保存为带标签的 CSV。

本工具默认采用“追加写入”策略：
1. 旧的 0~8 类历史数据会完整保留；
2. 新增“手势三”时，只需要继续往同一份 `gesture_data.csv` 里追加 `label=9` 的样本；
3. 新增类别命令为 `T / THREE / G9`。

板端配合方式：
1. `uart` 工程正常运行后，按一次 SW2，进入采集模式；
2. 采集模式下 LED 常亮，`uart7` 连续输出 10 列 CSV：
   f0x,f0y,f1x,f1y,f2x,f2y,f3x,f3y,roll,pitch
3. 再按一次 SW2，退出采集模式，恢复识别与命令发送。

用法示例：
  python collect_data.py --port COM7
"""

from __future__ import annotations

import argparse
import csv
import os
import sys
import threading
import time
from dataclasses import dataclass, field

import serial
from tool_config import (
    CSV_COLUMNS,
    DATASET_TARGET_FRAMES,
    DEFAULT_DATASET_PATH,
    FEATURE_NAMES,
    GESTURE_COLLECTION_COMMANDS,
    GESTURE_LABELS,
    LIVE_DISPLAY_INTERVAL,
)


@dataclass
class CollectorState:
    """采集线程和命令线程共享的运行状态。"""

    collecting: bool = False
    current_label: int = -1
    running: bool = True
    frame_counter: int = 0
    total_lines: int = 0
    last_raw: str = ""
    last_ncols: int = 0
    counts: dict[int, int] = field(default_factory=lambda: {label: 0 for label in GESTURE_LABELS})
    lock: threading.Lock = field(default_factory=threading.Lock)


def ensure_parent_dir(path: str) -> None:
    """按需创建输出文件所在目录。"""
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)


def open_csv(csv_path: str) -> tuple[object, csv.writer]:
    """打开 CSV 文件；如果是新文件，就先写表头。"""
    ensure_parent_dir(csv_path)
    need_header = not os.path.exists(csv_path)
    csv_file = open(csv_path, "a", newline="", encoding="utf-8")
    writer = csv.writer(csv_file)
    if need_header:
        writer.writerow(CSV_COLUMNS)
        csv_file.flush()
    return csv_file, writer


def load_existing_counts(csv_path: str, state: CollectorState) -> None:
    """启动时读取历史数据量，方便继续补采。"""
    if not os.path.exists(csv_path):
        return

    with open(csv_path, "r", encoding="utf-8") as csv_file:
        reader = csv.DictReader(csv_file)
        if "label" not in (reader.fieldnames or []):
            return

        for row in reader:
            try:
                label = int(row["label"])
            except (TypeError, ValueError):
                continue

            if label in state.counts:
                state.counts[label] += 1


def delete_rows(csv_path: str, label: int, state: CollectorState) -> None:
    """删除某一类全部数据，并重写 CSV。"""
    if not os.path.exists(csv_path):
        print(f"[提示] 数据文件不存在: {csv_path}")
        return

    with state.lock:
        with open(csv_path, "r", encoding="utf-8") as csv_file:
            rows = list(csv.reader(csv_file))

        if not rows:
            print("[提示] 数据文件为空。")
            return

        header = rows[0]
        data_rows = rows[1:]
        if "label" not in header:
            print("[错误] CSV 缺少 label 列，无法删除。")
            return

        label_index = header.index("label")
        filtered_rows = [row for row in data_rows if len(row) > label_index and row[label_index] != str(label)]
        deleted = len(data_rows) - len(filtered_rows)

        with open(csv_path, "w", newline="", encoding="utf-8") as csv_file:
            writer = csv.writer(csv_file)
            writer.writerow(header)
            writer.writerows(filtered_rows)

        state.counts[label] = 0

    print(f"[删除] 已删除 label={label} ({GESTURE_LABELS[label]}) 的 {deleted} 帧数据。")


def print_help() -> None:
    """打印命令帮助。"""
    print("=" * 72)
    print("RA6M5 手势数据采集工具")
    print("=" * 72)
    print("开始前请先按开发板 SW2 进入采集模式，进入后 LED 会常亮。")
    print()
    print("采集命令：")
    print("  G0~G4        采集 fist / open / one / two / rock")
    print("  U/D/L/R      采集 up / down / left / right")
    print("  T / THREE    采集 three（也支持 G9）")
    print("  OVER         停止当前采集")
    print("  STATUS       查看每一类当前已采集数量")
    print("  DELETE <n>   删除某一类全部数据，支持 0~9")
    print("  QUIT / Q     保存并退出")
    print()
    print("说明：默认对已有 CSV 做追加写入，原来 0~8 类历史数据会完整保留。")
    print(f"建议每一类至少采集 {DATASET_TARGET_FRAMES} 帧。")
    print("=" * 72)


def show_status(state: CollectorState) -> None:
    """显示每一类当前已经采集到的帧数。"""
    with state.lock:
        counts_snapshot = dict(state.counts)

    print()
    print("当前数据统计：")
    for label, name in GESTURE_LABELS.items():
        count = counts_snapshot[label]
        remain = max(0, DATASET_TARGET_FRAMES - count)
        tip = "已达标" if remain == 0 else f"还差 {remain} 帧"
        print(f"  [{label}] {name:>5s}: {count:4d} 帧  {tip}")
    print(f"  合计: {sum(counts_snapshot.values())} 帧")
    print()


def print_live(values: list[float]) -> None:
    """按固定间隔打印一行实时特征，方便观察串口数据是否正常。"""
    summary = "  ".join(f"{name}:{value:7.2f}" for name, value in zip(FEATURE_NAMES, values))
    print(f"  [实时] {summary}")
    print(">>> ", end="", flush=True)


def serial_reader(
    ser: serial.Serial,
    csv_file: object,
    writer: csv.writer,
    state: CollectorState,
) -> None:
    """
    串口后台线程：
    1. 持续读取开发板输出的 10 列特征；
    2. 当前处于采集状态时，再把数据写入 CSV。
    """
    warned_bad_cols = False

    while state.running:
        try:
            raw = ser.readline().decode("utf-8", errors="ignore").strip()
        except Exception:
            continue

        if not raw:
            continue

        parts = raw.split(",")

        with state.lock:
            state.total_lines += 1
            state.last_raw = raw
            state.last_ncols = len(parts)

        if len(parts) != len(FEATURE_NAMES):
            if not warned_bad_cols:
                print(f"\n[警告] 串口收到 {len(parts)} 列，期望 {len(FEATURE_NAMES)} 列。")
                print("       请确认开发板已经切到采集模式，且 `uart7` 没有混入其他输出。")
                warned_bad_cols = True
            continue

        warned_bad_cols = False

        try:
            values = [float(item) for item in parts]
        except ValueError:
            continue

        with state.lock:
            if state.collecting and state.current_label >= 0:
                writer.writerow(values + [state.current_label])
                csv_file.flush()
                state.counts[state.current_label] += 1
                state.frame_counter += 1

                if state.frame_counter % LIVE_DISPLAY_INTERVAL == 0:
                    print_live(values)


def print_ready_hint(state: CollectorState) -> None:
    """启动后等待 2 秒，给出一次串口就绪诊断。"""
    time.sleep(2)

    with state.lock:
        total_lines = state.total_lines
        last_ncols = state.last_ncols
        last_raw = state.last_raw

    if total_lines == 0:
        print("[警告] 2 秒内没有收到任何串口数据。")
        print("       请检查串口号、波特率、硬件连线，以及开发板是否已进入采集模式。")
        return

    if last_ncols != len(FEATURE_NAMES):
        print(f"[警告] 已收到串口数据，但最后一行列数为 {last_ncols}，不是 {len(FEATURE_NAMES)}。")
        print(f"       最后一行示例: {last_raw[:80]}")
        print("       请确认开发板已经切到采集模式。")
        return

    print(f"[就绪] 已收到 {total_lines} 行 {len(FEATURE_NAMES)} 列特征数据，可以开始采集。")


def start_collect(label: int, state: CollectorState) -> None:
    """开始采集某一个标签。"""
    with state.lock:
        state.collecting = True
        state.current_label = label
        state.frame_counter = 0

    print(
        f"[采集中] label={label} ({GESTURE_LABELS[label]})，"
        "保持手势稳定，输入 OVER 停止当前类别采集。"
    )


def stop_collect(state: CollectorState) -> None:
    """停止当前采集任务。"""
    with state.lock:
        label = state.current_label
        state.collecting = False
        state.current_label = -1

    if label < 0:
        print("[提示] 当前没有正在进行的采集任务。")
        return

    print(f"[停止] label={label} ({GESTURE_LABELS[label]}) 当前累计 {state.counts[label]} 帧。")


def parse_args() -> argparse.Namespace:
    """解析命令行参数。"""
    parser = argparse.ArgumentParser(description="RA6M5 手势数据采集工具")
    parser.add_argument("--port", required=True, help="串口号，例如 COM7")
    parser.add_argument("--baud", type=int, default=115200, help="串口波特率，默认 115200")
    parser.add_argument("--out", default=DEFAULT_DATASET_PATH, help="输出 CSV 文件路径")
    return parser.parse_args()


def main() -> None:
    """工具主流程：打开串口、启动后台线程、处理用户命令。"""
    args = parse_args()
    state = CollectorState()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except Exception as exc:
        print(f"[错误] 无法打开串口 {args.port}: {exc}")
        sys.exit(1)

    load_existing_counts(args.out, state)
    csv_file, writer = open_csv(args.out)

    print(f"[串口] 已连接 {args.port} @ {args.baud}")
    print(f"[文件] 数据将保存到: {os.path.abspath(args.out)}")
    print("[模式] 当前为追加采集模式，不会覆盖原有 0~8 类历史样本。")

    reader_thread = threading.Thread(
        target=serial_reader,
        args=(ser, csv_file, writer, state),
        daemon=True,
    )
    reader_thread.start()

    print_ready_hint(state)
    print_help()
    show_status(state)

    try:
        while True:
            try:
                command = input(">>> ").strip()
            except (EOFError, KeyboardInterrupt):
                break

            if not command:
                continue

            parts = command.split()
            op = parts[0].upper()

            if op in GESTURE_COLLECTION_COMMANDS:
                start_collect(GESTURE_COLLECTION_COMMANDS[op], state)
                continue

            if op == "OVER":
                stop_collect(state)
                show_status(state)
                continue

            if op == "STATUS":
                show_status(state)
                continue

            if op == "DELETE":
                if len(parts) < 2:
                    print("[提示] 用法: DELETE <0~9>")
                    continue

                try:
                    label = int(parts[1])
                except ValueError:
                    print("[错误] DELETE 后面必须是整数标签。")
                    continue

                if label not in GESTURE_LABELS:
                    print("[错误] 标签范围必须是 0~9。")
                    continue

                confirm = input(f"确认删除 [{label}] {GESTURE_LABELS[label]} 的全部数据？(y/n) ").strip().lower()
                if confirm == "y":
                    with state.lock:
                        if state.collecting and state.current_label == label:
                            state.collecting = False
                    delete_rows(args.out, label, state)
                    show_status(state)
                continue

            if op in ("QUIT", "Q"):
                break

            print("[未知命令] 可用命令: G0~G4, U/D/L/R, T/THREE/G9, OVER, STATUS, DELETE <n>, QUIT")

    finally:
        state.running = False
        with state.lock:
            state.collecting = False

        try:
            csv_file.close()
        except Exception:
            pass

        ser.close()
        print(f"\n[退出] 数据已保存到: {os.path.abspath(args.out)}")
        show_status(state)


if __name__ == "__main__":
    main()
