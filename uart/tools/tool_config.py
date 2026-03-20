"""
tool_config.py
==============
统一维护 `uart/tools` 工具链共用的配置，避免采集脚本和训练脚本各自维护一份标签表。

这里的设计目标有两个：
1. 保证“特征顺序 / 标签编号 / 默认文件路径”只有一个真源，减少后续扩展手势时的改动面；
2. 让新增“手势三”时可以继续沿用原来的 `gesture_data.csv`，只是在同一份数据集里追加 `label=9` 的样本。
"""

from __future__ import annotations

import os


FEATURE_NAMES = [
    "f0x",
    "f0y",
    "f1x",
    "f1y",
    "f2x",
    "f2y",
    "f3x",
    "f3y",
    "roll",
    "pitch",
]

CSV_COLUMNS = FEATURE_NAMES + ["label"]

GESTURE_LABELS = {
    0: "fist",
    1: "open",
    2: "one",
    3: "two",
    4: "rock",
    5: "up",
    6: "down",
    7: "left",
    8: "right",
    9: "three",
}

GESTURE_COLLECTION_COMMANDS = {
    "G0": 0,
    "G1": 1,
    "G2": 2,
    "G3": 3,
    "G4": 4,
    "U": 5,
    "UP": 5,
    "D": 6,
    "DOWN": 6,
    "L": 7,
    "LEFT": 7,
    "R": 8,
    "RIGHT": 8,
    "T": 9,
    "THREE": 9,
    "G9": 9,
}

DATASET_TARGET_FRAMES = 200
LIVE_DISPLAY_INTERVAL = 20

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DATASET_PATH = os.path.join(TOOLS_DIR, "data", "gesture_data.csv")
DEFAULT_GENERATED_MODEL_PATH = os.path.join(TOOLS_DIR, "generated", "gesture_model.h")
DEFAULT_FIRMWARE_MODEL_PATH = os.path.abspath(
    os.path.join(TOOLS_DIR, "..", "src", "gesture", "gesture_model.h")
)

