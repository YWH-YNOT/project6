# `uart/tools` 使用说明

这里提供 `uart` 工程自己的手势数据采集与 SVM 训练工具链，不再依赖 `RS_bei/shoutao_you/tools`。

当前工具链已经升级为 10 类标签：

- `0~8` 保持原有 `fist/open/one/two/rock/up/down/left/right`
- `9` 新增为 `three`

## 目录说明

- `tool_config.py`
  统一维护特征顺序、标签编号、采集命令和默认文件路径
- `collect_data.py`
  从开发板 `uart7` 采集 10 维特征流，并保存为本地 `CSV`
- `train_svm.py`
  读取 `CSV` 训练线性 `SVM`，并同步导出模型头文件
- `data/gesture_data.csv`
  默认数据集保存位置
- `generated/gesture_model.h`
  工具侧留档用的最新模型头文件
- `../src/gesture/gesture_model.h`
  固件编译时实际使用的模型头文件

## 板端工作模式

`uart` 固件有两种互斥模式：

1. 识别模式
   - 默认上电即进入
   - `uart7` 只发送 1 字节控制命令
   - LED 熄灭，识别到有效命令时会闪烁
2. 采集模式
   - 按一次开发板 `SW2` 进入
   - `uart7` 连续输出 10 列特征 `CSV`
   - LED 常亮
   - 再按一次 `SW2` 退出，恢复识别模式

## 第一步：安装 Python 依赖

```bash
pip install numpy pandas scikit-learn pyserial
```

## 第二步：采集手势数据

1. 烧录 `uart` 工程
2. 将上位机连接到开发板 `uart7`
3. 按一次 `SW2` 进入采集模式，确认 LED 常亮
4. 运行：

```bash
cd uart/tools
python collect_data.py --port COM7
```

5. 在工具里输入类别命令开始采集：

```text
G0~G4          -> fist/open/one/two/rock
U/D/L/R        -> up/down/left/right
T/THREE/G9     -> three
OVER           -> 停止当前类别
STATUS         -> 查看统计
DELETE <n>     -> 删除某一类
Q              -> 退出
```

说明：

- `collect_data.py` 默认对现有 `gesture_data.csv` 做追加写入，不会覆盖之前已经采集好的 0~8 类样本。
- 新增“手势三”时，只需要继续在同一份 CSV 中补采 `label=9` 的数据。

## 第三步：训练 10 类 SVM 并导出权重

```bash
cd uart/tools
python train_svm.py
```

训练前提：

- 数据集中必须完整覆盖 `0~9` 全部标签。
- 如果当前还没有 `three(label=9)` 数据，脚本会直接报错提醒，不会破坏旧数据文件。

训练完成后，脚本会同时更新：

- `uart/tools/generated/gesture_model.h`
- `uart/src/gesture/gesture_model.h`

## 第四步：在工程里使用新模型

后续更新模型时，只需要：

1. 继续补采 `CSV`
2. 重新运行 `python train_svm.py`
3. 重新编译 `uart` 工程

不需要再手工复制或修改 `uart/src` 里的 SVM 权重数组。
