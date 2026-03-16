"""
merge_csv.py — 一次性CSV合并迁移工具
======================================
将旧的 dir_gesture_data.csv（标签0-3）合并进 gesture_data.csv，
方向手势标签自动 +5 偏移（变为5/6/7/8），与原有手势（0-4）不冲突。

运行完成后即可直接运行 train_svm.py 训练9类统一模型。
"""
import os
import csv
import sys

GESTURE_FILE = "gesture_data.csv"
DIR_FILE     = "dir_gesture_data.csv"
DIR_OFFSET   = 5   # dir标签 0-3 → 5-8

def main():
    if not os.path.exists(DIR_FILE):
        print(f"[提示] 未找到 {DIR_FILE}，跳过合并")
        return

    # 读取 dir_gesture_data.csv
    with open(DIR_FILE, "r", encoding="utf-8") as f:
        reader = csv.reader(f)
        rows = list(reader)

    if not rows:
        print(f"[提示] {DIR_FILE} 为空，跳过")
        return

    # 判断有无表头
    try:
        float(rows[0][0])
        header = None
        data   = rows
    except ValueError:
        header = rows[0]
        data   = rows[1:]

    print(f"[信息] {DIR_FILE} 共 {len(data)} 行数据（含表头{1 if header else 0}行）")

    # 读取目标文件表头（若存在）
    if os.path.exists(GESTURE_FILE):
        with open(GESTURE_FILE, "r", encoding="utf-8") as f:
            peek = list(csv.reader(f))
        dst_header = peek[0] if peek else None
    else:
        dst_header = None

    # 转换标签并追加
    need_header = not os.path.exists(GESTURE_FILE)
    added = 0
    skipped = 0
    with open(GESTURE_FILE, "a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        if need_header:
            # 自动构建表头
            writer.writerow(["f0x","f0y","f1x","f1y","f2x","f2y","f3x","f3y","roll","pitch","label"])

        for row in data:
            if len(row) < 2:
                skipped += 1
                continue
            try:
                label = int(float(row[-1]))
                new_row = row[:-1] + [str(label + DIR_OFFSET)]
                writer.writerow(new_row)
                added += 1
            except ValueError:
                skipped += 1  # 跳过非数字行

    print(f"[完成] 已将 {added} 行数据（标签 +{DIR_OFFSET}）追加到 {GESTURE_FILE}")
    if skipped:
        print(f"[提示] 跳过了 {skipped} 行（表头或无效行）")
    print()
    print(f"  现在可以运行：python train_svm.py")
    print(f"  之后将生成的 gesture_model.h 复制到 HARDWARE/ 目录")

if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    main()
