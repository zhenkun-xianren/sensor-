# NEC 红外遥控驱动

## 适用范围

来源实现识别 NEC 脉冲帧并产生事件。帧解码可复用的核心 API 为 `PortIr_DecoderInit`、`PortIr_FeedPulse`；目前它们与队列、Device 读取接口和 BSP 调用共同位于 `port_ir_remote.c`，尚未拆成纯协议库。

## 文件与分层

- `Device/inc/dev_ir_remote.h`：遥控事件数据类型。
- `Core/Inc/port_ir_remote.h`、`Core/Src/port_ir_remote.c`：NEC 解码和事件队列。
- `Core/Inc/bsp_ir.h`、`Core/Src/bsp_ir.c`：STM32F103 TIM3 微秒计数、PA8/EXTI8 边沿采集；依赖 `平台适配/STM32F103C8T6` 的 GPIO/板级头文件及 STM32 CMSIS。

## 移植要点

新 MCU 上应提供等效的脉冲电平与微秒时长，确认定时器回绕、EXTI 映射、输入上拉和 NEC 容差；按该板重写 BSP。事件队列大小为编译期配置，队列满时事件会丢弃。APP 消费示例留在来源工程，未随驱动归档。

## 验证状态

仅从来源快照抽取；本归档未单独编译或在其他 MCU/接收头验证。
