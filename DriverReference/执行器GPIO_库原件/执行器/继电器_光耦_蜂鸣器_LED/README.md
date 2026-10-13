# 执行器与指示灯接口

## 适用范围

来源硬件包括继电器、PC817 隔离输出、蜂鸣器以及 WARNING/POWER 指示灯。实现直接针对 STM32F103C8T6 当前板卡 PA1/PA5/PA6/PB 指示灯引脚和输出极性。

## 文件与分层

- `Device/inc/dev_actuator.h`：执行器接口。
- `Core/Inc/port_actuator.h`、`Core/Src/port_actuator.c`：Device 到板级接口的绑定。
- `Core/Inc/bsp_actuator.h`、`Core/Src/bsp_actuator.c`：GPIO 输出实现；依赖根目录 `平台适配/STM32F103C8T6` 的 `bsp_gpio.h`、`bsp_board.h` 和 STM32F1 CMSIS。

## 移植要点

先核对驱动电路、隔离器件、有效电平和复位安全状态，再重写 BSP。蜂鸣器/指示灯的业务闪烁和 FreeRTOS 定时器逻辑属于来源 APP，不包含在本驱动模块。

## 验证状态

仅从来源快照抽取；本归档未单独编译或在其他板卡验证。
