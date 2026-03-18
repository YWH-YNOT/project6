#ifndef GESTURE_GESTURE_MODEL_H_
#define GESTURE_GESTURE_MODEL_H_

/*
 * 瑞萨端模型适配层
 * 1. 不在工程源码里手写 SVM 权重；
 * 2. 直接包含上位机训练工具导出的头文件；
 * 3. 以后重新采集并训练时，只需要替换导出的权重文件即可。
 */
#define GESTURE_MODEL_EXPORT_HEADER "../../tools/generated/gesture_model.h"

#include GESTURE_MODEL_EXPORT_HEADER

/*
 * 当前串口协议固定输出 10 维特征：
 * f0x,f0y,f1x,f1y,f2x,f2y,f3x,f3y,roll,pitch
 * 如果导出的模型维度不一致，直接在编译期报错，避免带着隐患运行。
 */
#if (GM_N_FEATURES != 10)
#error "RA6M5 当前协议层固定输出 10 维特征，请导出 10 维 gesture_model.h"
#endif

/*
 * 当前业务层的 label->cmd 映射固定按照 9 类手势编写：
 * fist/open/one/two/rock/up/down/left/right
 * 如果类别数变化，业务层命令映射也必须一起调整。
 */
#if (GM_N_CLASSES != 9)
#error "RA6M5 当前业务层命令映射固定为 9 类，请导出 9 类 gesture_model.h"
#endif

#endif /* GESTURE_GESTURE_MODEL_H_ */
