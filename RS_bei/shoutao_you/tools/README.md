# 手势识别 SVM 工具链使用说明

## 工具清单

| 工具 | 用途 |
|------|------|
| `collect_data.py`         | **统一采集工具**：采集原有5种手势 + 4种方向手势 |
| `train_svm.py`            | 训练原有手势分类器 → 生成 `gesture_model.h` |
| `train_dir_gesture_svm.py`| 训练方向手势分类器 → 生成 `dir_gesture_model.h` |
| `collect_swipe.py`        | 采集**挥手方向**数据（左/右/上/下）|
| `train_swipe_svm.py`      | 训练挥手分类器 → 生成 `swipe_model.h` |
| `test_gesture.py`         | PC端实时验证手势分类效果 |

---

## 新功能：握拳激活 + 方向手势控制

用**握拳**激活方向控制模式，激活后用**4种自定义手势**控制上下左右。

```
握拳（保持约1.5s）→ 激活方向控制
  ↳ 做方向手势（保持约1.5s）→ 发出方向命令
  ↳ 再次握拳（保持约1.5s）→ 退出
  ↳ 3秒无手势 → 自动超时退出
```

### 第一步：采集数据

```bash
cd tools
# 修改 collect_data.py 顶部的 PORT（如 COM3）
python collect_data.py
```

**采集4种方向手势（你自定义的手势）：**

| 命令 | 采集的方向 |
|------|-----------|
| `U` 或 `UP` | ↑ 上 |
| `D` 或 `DOWN` | ↓ 下 |
| `L` 或 `LEFT` | ← 左 |
| `R` 或 `RIGHT` | → 右 |

**采集原有5种手势（可选，用于激活检测更新）：**

| 命令 | 手势 |
|------|------|
| `G0` | 握拳 fist（激活键） |
| `G1` | 张开 open |
| `G2` | 比1 one |
| `G3` | 比2/V two |
| `G4` | 摇滚 rock |

**通用命令：**
- `OVER` — 停止当前采集
- `status` — 查看所有数量（目标每类 ≥ 200 帧）
- `DELETE G<n>` / `DELETE D<n>` — 删除对应数据
- `quit` / `q` — 退出保存

---

### 第二步：训练方向手势模型

```bash
python train_dir_gesture_svm.py
# 输出：dir_gesture_model.h，5折交叉验证目标准确率 ≥ 85%
```

### 第三步：训练原有手势模型（握拳识别器）

```bash
python train_svm.py
# 输出：gesture_model.h
```

---

### 第四步：集成到 STM32

1. 将 `dir_gesture_model.h` 复制到 `HARDWARE/` 目录
2. 将 `gesture_model.h` 复制到 `HARDWARE/` 目录（如有更新）
3. 在 `main.c` 的主循环中调用：

```c
Gesture_DirControl();   // 握拳激活 + 方向手势控制
```

4. 用 Keil 重新编译烧录
5. 串口助手观察输出：

```
[方向控制] 已激活，做手势控制方向，再次握拳退出
[方向手势] up
[方向控制] 握拳退出
```

---

### 参数调整（control.c 顶部）

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `DC_CONFIRM` | 15帧 | 连续帧确认防抖（~150ms @ 100Hz） |
| `DC_TIMEOUT` | 300帧 | ACTIVE模式超时（~3s @ 100Hz） |
| `DC_CD_FRAMES` | 40帧 | 每次命令后冷却（~0.4s @ 100Hz） |

---

## 挥手识别（原有功能，保留不变）

### 快速开始

```bash
# 采集挥手数据
python collect_swipe.py   # 命令：L/R/U/D

# 训练
python train_swipe_svm.py
# 输出：swipe_model.h，5折交叉验证目标 ≥ 85%
```

在 `main.c` 主循环中调用：
```c
Gesture_Move();  // 挥手方向识别
```

串口输出：`[挥手 SVM] left  (peak=32.4)`

### 调参（control.c 顶部）

| 参数 | 默认值 | 效果 |
|------|--------|------|
| `SWIPE_DEPART_THRESH` | 25° | 出发阈值，降低=更灵敏 |
| `SWIPE_RETURN_THRESH` | 10° | 回归阈值 |
| `SWIPE_PEAK_MIN`      | 25° | 峰值下限，升高=防误触 |
| `SWIPE_COOLDOWN_FRAMES` | 60帧 | 冷却时间 |

---

## 手势编号对照

### 方向手势（dir_gesture_model.h）

| 编号 | 方向 |
|------|------|
| 0 | ↑ 上 (up) |
| 1 | ↓ 下 (down) |
| 2 | ← 左 (left) |
| 3 | → 右 (right) |

### 原有手势（gesture_model.h）

| 编号 | 名称 | 说明 |
|------|------|------|
| 0 | fist | 握拳（**方向控制激活键**） |
| 1 | open | 全部张开 |
| 2 | one  | 仅食指伸展 |
| 3 | two  | 食指+中指（V字） |
| 4 | rock | 食指+小指伸展 |

### 挥手方向（swipe_model.h）

| 编号 | 意义 |
|------|------|
| 0 | 向左挥 (left) |
| 1 | 向右挥 (right) |
| 2 | 向上挥 (up) |
| 3 | 向下挥 (down) |
