#ifndef PORT_FLAME_H
#define PORT_FLAME_H

#include "dev_flame.h"

/* 手册第5～6页协议参数，不含板级引脚或寄存器。 */
#define PORT_FLAME_CONFIG_BITS       25U // SERIN 一次必须写入的位数。
#define PORT_FLAME_FRAME_BITS        40U // DOCI 完整状态及配置帧位数。
#define PORT_FLAME_CONFIG_MASK       0x01FFFFFFUL // bit24:0 配置字段。
#define PORT_FLAME_ADC_MASK          0x00003FFFUL // 14 位 ADC 原始码。
#define PORT_FLAME_RANGE_SHIFT       39U // 超范围指示位，0 表示超范围。
#define PORT_FLAME_RANGE_FAULT        0ULL // bit39 的超范围编码。
#define PORT_FLAME_ADC_SHIFT         25U // ADC 字段低位位置。
#define PORT_FLAME_SENSITIVITY_SHIFT 17U // 灵敏度字段位置。
#define PORT_FLAME_BLIND_SHIFT       13U // 盲锁时间字段位置。
#define PORT_FLAME_PULSE_SHIFT       11U // 脉冲计数器字段位置。
#define PORT_FLAME_WINDOW_SHIFT       9U // 时间窗口字段位置。
#define PORT_FLAME_DETECT_SHIFT       8U // 检测使能字段位置。
#define PORT_FLAME_INTERRUPT_SHIFT    7U // 中断源字段位置。
#define PORT_FLAME_SOURCE_SHIFT       5U // ADC 源字段位置。
#define PORT_FLAME_VREG_DISABLED      (1UL << 4U) // 手册要求 VREG 必须关闭。
#define PORT_FLAME_HPF_SHIFT          2U // 高通配置字段位置。
#define PORT_FLAME_BLIND_MAX         15U // 4 位盲锁字段的最大码。
#define PORT_FLAME_COUNT_MAX          3U // 2 位脉冲/窗口字段的最大码。

#endif
