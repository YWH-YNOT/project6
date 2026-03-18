# `uart/tools` 使用说明

这里提供 `uart` 工程自己的手势数据采集与 SVM 训练工具链，不再依赖 `RS_bei/shoutao_you/tools`。

## 目录说明

- `collect_data.py`
  从开发板 `uart7` 采集 10 维特征流，并保存为本地 `CSV`
- `train_svm.py`
  读取 `CSV` 训练线性 `SVM`，导出 C 头文件
- `generated/gesture_model.h`
  当前固件实际包含的权重文件
- `data/gesture_data.csv`
  默认数据集保存位置

## 板端工作模式

`uart` 固件增加了两种互斥模式：

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
G0~G4   -> fist/open/one/two/rock
U/D/L/R -> up/down/left/right
OVER    -> 停止当前类别
STATUS  -> 查看统计
DELETE n-> 删除某一类
Q       -> 退出
```

采集完成后，数据默认保存在：

`uart/tools/data/gesture_data.csv`

## 第三步：训练 SVM 并导出权重

```bash
cd uart/tools
python train_svm.py
```

默认会生成：

`uart/tools/generated/gesture_model.h`

## 第四步：在工程里使用新模型

`uart/src/gesture/gesture_model.h` 已经固定包含：

`uart/tools/generated/gesture_model.h`

所以你以后更新模型时，只需要：

1. 重新采集 `CSV`
2. 重新运行 `python train_svm.py`
3. 重新编译 `uart` 工程

不需要再手工修改 `uart/src` 里的 SVM 权重数组。
