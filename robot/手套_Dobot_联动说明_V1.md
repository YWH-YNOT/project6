# 手套 -> Dobot 联动说明 V1

## 1. 版本目标

本版本只做最小闭环验证：

- 手套/RA 侧继续输出现有原始命令字节
- PC 侧串口接收单字节命令
- Python 调用 Dobot 官方桌面 API
- Dobot 按离散增量执行一步动作

当前**不包含**：

- RA8P1 接入
- 视觉、雷达、小车
- 连续轨迹跟随
- 高级异常恢复

## 2. 文件说明

以下文件现统一收纳在 `robot/` 目录下：

- `glove_to_dobot_v1.py`
  主程序。负责读取串口、做命令去抖/节流、调用 Dobot API、执行软限位保护。
- `config.yaml`
  运行配置。你第一次上手主要改这个文件。
- `requirements.txt`
  Python 依赖。
- `third_party/dobot_magician_py64/`
  从本地官方 demo `demo-magician-python-64-master.zip` 整理出的 64 位 Python SDK 与 DLL。
- `logs/glove_to_dobot_v1.log`
  运行时自动生成的日志文件。

## 3. 命令映射

当前程序严格按现有工程命令语义做 PC 侧联动：

| 原始字节 | 来源手势 | V1 机械臂动作 |
| --- | --- | --- |
| `0x06` | `left` | `Y` 正方向一步 |
| `0x07` | `right` | `Y` 负方向一步 |
| `0x08` | `up` | `Z` 正方向一步 |
| `0x0A` | `down` | `Z` 负方向一步 |
| `0x69` | `one` | `X` 正方向一步 |
| `0x19` | `three` | 返回安全位 |
| `0x05` | `rock` | 特殊动作占位，默认只记日志 |
| `0x02` / `0x00` | - | 忽略，不参与联动 |

## 4. 安全策略

程序内置了这几层保护：

- `X/Y/Z` 软限位。目标点越界时会自动裁剪并打日志。
- 最小命令间隔。默认 `0.35s`。
- 重复字节去抖。默认 `0.08s` 内的重复字节直接丢弃。
- 动作执行期间不接收新动作。执行结束后会清空执行期间积压的串口输入，防止“补发”导致乱跳。
- 启动前人工确认提示。
- `Ctrl+C` 时会做一次 best-effort 的 `ForceStop + ClearQueue`。

## 5. 第一次需要改的配置

优先编辑 `config.yaml`：

1. `serial.port`
   改成手套/RA 命令输出连接到 PC 的串口，例如 `COM7`。
2. `dobot.port`
   如果 Dobot 自动搜索不稳定，建议明确填实际 COM 口；如果只有一台设备，也可以先保留空字符串。
3. `safety.safe_pose`
   改成你现场确认过的安全起始位。
4. `safety.soft_limits`
   按你的机械臂安装姿态、工装和末端工具重新确认范围。
5. `control.special_action_mode`
   现场如果确认已经装了夹爪，再从 `placeholder` 改成 `gripper`。

## 6. 安装依赖

在项目根目录执行：

```powershell
python -m pip install -r robot/requirements.txt
```

说明：

- 本项目优先假设你使用的是 **64 位 Python**。
- 本地已整理的是 `demo-magician-python-64-master.zip`，因此 Python 也必须是 64 位。

## 7. 运行方法

### 7.1 先做无硬件动作自检

```powershell
python robot/glove_to_dobot_v1.py --dry-run --single-command 0x69 --no-confirm
```

这个命令不会连接真实机械臂，只会验证：

- 配置能否读取
- 主程序能否启动
- 命令解析和目标 Pose 计算是否正常
- 日志能否正常输出

### 7.2 先单条测试 Dobot 动作

建议先做单命令测试，不要一上来就接手套串口：

```powershell
python robot/glove_to_dobot_v1.py --single-command 0x19
```

用途：

- 验证 Dobot 能否连接
- 验证基础初始化是否成功
- 验证安全位移动是否正常

再测试一个单步：

```powershell
python robot/glove_to_dobot_v1.py --single-command 0x69
```

### 7.3 最后接手套串口连续运行

```powershell
python robot/glove_to_dobot_v1.py
```

程序启动后会：

1. 提示你先确认机械臂处于安全工作区
2. 连接 Dobot
3. 清报警、清队列、设置运动参数
4. 读取当前 Pose
5. 自动回到 `config.yaml` 中配置的安全位
6. 开始监听手套串口

## 8. 第一次上手机械测试建议

建议按下面顺序测，不要跳步：

1. 机械臂断电时先确认固定牢靠，周围无遮挡。
2. 核对 `config.yaml` 中的 `safe_pose` 和 `soft_limits`。
3. 上电后先跑 `--single-command 0x19`，确认能回安全位。
4. 再跑 `--single-command 0x69`，看 `X+` 单步是否合理。
5. 再测 `0x06 / 0x07 / 0x08 / 0x0A` 四个方向。
6. 最后再接手套串口跑常驻模式。
7. 如果方向和你现场理解不一致，只改 PC 侧映射说明，不要先改手套端协议。

## 9. 日志说明

程序会同时输出控制台日志和文件日志，默认文件位置：

```text
robot/logs/glove_to_dobot_v1.log
```

日志里至少会看到：

- 收到的原始命令字节
- Dobot 连接状态
- 当前 Pose
- 每次动作的目标 Pose
- 软限位裁剪日志
- 运行异常日志

## 10. 当前已知限制

- `0x05` 默认只是特殊动作占位；若没有夹爪，不会真的驱动末端。
- 当前只做单步离散控制，不做连续轨迹跟随。
- 当前只保留 `X+ / Y+ / Y- / Z+ / Z- / safe-home` 这些最小动作。
- 由于优先复用了官方 demo 包，若 Dobot 在运动中 USB 链路异常断开，best-effort 停机仍可能不如完整工业控制器那样强健。

## 11. 现场建议

- 第一次运行时，把 `step_x_mm / step_y_mm / step_z_mm` 先设小一点，比如 `5mm`。
- 如果发现重复动作太密，可以适当增大 `min_command_interval_s`。
- 如果发现同一个命令偶尔被误触发两次，可以适当增大 `duplicate_debounce_s`。
