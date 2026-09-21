# RA6M5 智能手套手势识别链路

将 STM32 手套采集的姿态特征发送到 Renesas RA6M5，在 RA6M5 端完成协议解析、SVM 分类、稳定确认和周期命令输出。

## 数据链路

```text
STM32 多传感器手套
  └─ 每 20 ms 发送特征帧
          ↓ UART2
RA6M5：同步帧头 → CRC16 → 定点特征还原
          ↓
      每 50 ms 更新样本
          ↓
      每 100 ms SVM 分类
          ↓
      连续结果稳定确认
          ↓
      每 200 ms UART7 输出命令
```

## 设计重点

- 串口持续收包，分类与发送使用独立节拍；
- 固定长度二进制帧包含帧头、版本、序号、时间戳、特征和 CRC16；
- Python 训练脚本直接导出 C 模型头文件，减少手工抄写错误；
- 无动作与禁止命令不会下发，其他命令需要连续分类一致后激活；
- 定时标志只锁存一次，主循环短时阻塞后不会补跑过期任务。

## 分层结构

| 层级 | 关键路径 | 职责 |
| --- | --- | --- |
| FSP / HAL | `uart/ra_gen/`、`uart/ra_cfg/` | 时钟、IO、UART、AGT 等底层实例 |
| 板级串口 | `uart/src/debug_uart/bsp_debug_uart.*` | 屏蔽 FSP UART 细节 |
| 协议 | `uart/src/debug_uart/glove_frame.*` | 帧同步、版本、CRC、字节序与特征解析 |
| 模型 | `uart/src/gesture/gesture_model.h` | MCU 端 SVM 参数与分类 |
| 业务 | `uart/src/gesture/gesture_service.*` | 分类节拍、命令映射与稳定确认 |
| 定时 | `uart/src/timer/` | 10/100/200 ms 节拍聚合 |
| 应用 | `uart/src/hal_entry.c` | 初始化、收帧、分类与命令发送 |

## 通信帧

当前协议总长度为 31 字节：

| 字段 | 字节数 | 说明 |
| --- | ---: | --- |
| SOF | 2 | `0xAA 0x55` |
| Version | 1 | 协议版本 |
| Sequence | 1 | 帧序号 |
| Tick | 4 | STM32 时间戳，小端 |
| Feature | 20 | 10 × `int16` 定点特征 |
| Status | 1 | 状态字 |
| CRC16 | 2 | CRC16-CCITT-FALSE，小端 |

## 模型工作流

```text
串口采集 → CSV 数据集 → Python 训练 SVM
        → 导出 gesture_model.h → RA6M5 编译与板端推理
```

相关工具位于 `uart/tools/`。训练与固件必须使用一致的特征顺序、量纲和标签映射。

## 文档入口

- [工程大纲](工程大纲.md)
- [工程框架](工程框架.md)
- [项目任务计划书](项目任务计划书.md)
- [RA6M5 迁移方案](RA6M5_智能姿态识别手套迁移方案.md)

## 证据边界

仓库能够证明协议、分层、训练工具与 RA6M5 工程实现；最终识别率、端到端延迟和机器人实机效果应以对应数据集、构建日志及现场验收记录为准。
