# STM32F103C8T6 GPIO 适配

## 适用范围

来源 GPIO Device/Port 接口和整板 GPIO 初始化。板级实现同时配置该项目的红外、RS485、甲烷 UART、显示与执行器引脚，属于本板适配，不是通用 GPIO 驱动。

## 文件与分层

- `Device/inc/dev_gpio.h`：GPIO 初始化设备接口，依赖 `共用/Device/inc/dev_common.h`。
- `Core/Inc/port_gpio.h`、`Core/Src/port_gpio.c`：状态转换与 Device 绑定。
- BSP 位于根目录 `平台适配/STM32F103C8T6`，由该模块及其他板级模块共用。

## 移植要点

将引脚定义与目标原理图、启动安全电平、复用和外设时钟逐项核对。不要在单一模块工程中无条件使用整板 `BSP_Gpio_Init`，否则会配置其他功能占用的引脚。

## 验证状态

仅从来源快照抽取；本归档未单独编译或在其他板卡验证。
