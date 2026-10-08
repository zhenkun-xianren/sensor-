# Modbus RTU 主站与 RS485 适配

## 功能与调用

Device/Port 为单实例主站，提供功能码 0x06、CRC16 和零基寄存器地址。仅允许一个任务串行调用，不支持广播地址 0，不自动重试。

- `sendHoldingRegister(self, slaveAddress, registerAddress, value)`：只发送；OK 只表示最后停止位已发出，不表示从站执行成功。
- `writeHoldingRegister(self, slaveAddress, registerAddress, value)`：发送后在整体 100 ms 期限内校验回显、CRC 和异常应答。
- 两者共用请求组帧和本地发送流程。GetModbus 只绑定静态对象；先完成板级时钟、毫秒时基和 GPIO，再调用 dev->init(dev)，并检查每次调用结果。
- 发送失败后须重新初始化，超时不重发本帧。接收超时、CRC/回显/异常失败不宣称从站写入成功。

```c
DevModbus *dev = GetModbus(); /* 单实例接口，串行使用。 */
DevModbusStatus result = dev->init(dev); /* 初始化结果。 */
if (result == DEV_MODBUS_OK) {
    result = dev->sendHoldingRegister(dev, 1U, 0x0000U, 0x0001U);
}
```

## 集中功能配置

`Device/inc/dev_modbus_config.h` 默认 `DEV_MODBUS_ENABLE_SEND=1`、`DEV_MODBUS_ENABLE_RESPONSE=1`，保留全部库能力。目标工程宏可覆盖默认值；所有编译单元必须使用同一配置，不要在单个源文件内临时改宏。

超声波工程的正式目标设置 `DEV_MODBUS_ENABLE_SEND=1,DEV_MODBUS_ENABLE_RESPONSE=0`，关闭应答方法声明、实现、绑定、应答专属常量及 RS485 参考接收依赖，源码保留。屏蔽函数及完整签名：

- `DevModbusStatus (*writeHoldingRegister)(DevModbus *self, uint8_t slaveAddress, uint16_t registerAddress, uint16_t value)`
- `static DevModbusStatus modbus_write_holding_register(DevModbus *self, uint8_t slaveAddress, uint16_t registerAddress, uint16_t value)`
- `static DevModbusStatus modbus_read_before_deadline(uint8_t *data, uint32_t startTick)`
- 库参考 BSP：`void BSP_Rs485_ClearRx(void)`、`BspRs485Status BSP_Rs485_ReadByte(uint8_t *data, uint32_t timeoutMs)`、`uint32_t BSP_Rs485_GetTickMs(void)`、`void BSP_Rs485_IrqHandler(void)`；环形缓冲区及专属 IRQ 配置同受 RESPONSE 宏控制。

关闭 SEND 时屏蔽 `DevModbusStatus (*sendHoldingRegister)(DevModbus *self, uint8_t slaveAddress, uint16_t registerAddress, uint16_t value)` 方法声明和绑定；内部发送和 CRC/组帧仍为应答功能所需，保持编译。至少启用一个操作。

恢复应答功能需先适配目标 BSP 接收、时基和中断，再统一将 RESPONSE 设为 1 并重新构建；当前 G071 只发送 BSP 不具备该能力，不能仅改宏就投入使用。

## 文件与平台边界

- `Device/inc/dev_modbus.h`、`dev_modbus_config.h`：无芯片或 RTOS 依赖的设备契约和功能配置。
- `Core/Inc/port_modbus.h`、`Core/Src/port_modbus.c`：共用 RTU 协议实现，与工程副本逐字节一致。
- `Core/Inc/bsp_rs485.h`、`Core/Src/bsp_rs485.c`：STM32F103 USART2 参考，TX=PA2、RX=PA3、DE与/RE=PA4；方向高发送、低释放，9600 bit/s、8N1，整体发送期限 50 ms。参考 GPIO 必须先准备引脚，BSP 不覆盖整板 GPIO。
- 全功能参考依赖 `共用/Core/Inc/bsp_byte_ring.h`、`共用/Core/Src/bsp_byte_ring.c`、平台 GPIO/板级定义、STM32F1 CMSIS 及来源 HAL 毫秒时基；还需由平台 USART2 中断入口调用 BSP_Rs485_IrqHandler。
- 来源参考 BSP 保留已有 HAL_GetTick，原因是来源工程以 HAL 提供系统毫秒时基；协议层不依赖 HAL。移植时以目标 BSP 时基替换，不能把此参考复制为目标工程完整启动实现。
- 当前超声波工程使用自有 STM32G071 BSP：USART3、PD8 TX、PA10 DE与/RE，不配置 PD9 RX；使用 BSP_Time_GetMs，64 MHz、Compiler 5。APP 每 1000 ms 发送 `01 06 00 00 00 01 48 0A`；这是工程应用配置，不是库默认自动业务。
- BSP 契约：Init/Send 返回命名状态；Send 等 TC 后释放总线，超时释放并关闭 UART。NOT_INITIALIZED 与参数错误区分。不同平台 BSP 枚举数值允许不同，Port 按符号映射。
- Modbus 枚举保留旧库数值 0 至 7；旧超声波只发送版本 NOT_INITIALIZED=4，本次统一为 7，现有 APP 只按枚举符号使用。

## 来源、移植与验证状态

本包由历史 STM32F103 来源快照维护，本次统一加入超声波工程只发送能力。核对收发器型号、DE/RE 极性、速率、终端、偏置、时基及中断，替换 BSP 并使用工程内副本和相对路径构建。

以本次分类更新日志记载的正式 STM32G071 Compiler 5 编译/链接结果作为通用代码入库依据；不代表库全功能配置或 F103 参考 BSP 已编译。软件测试未运行（未明确要求）；库参考 BSP 未单独编译；G071/F103 硬件均待用户验证，不沿用旧硬件结果证明新版本。
