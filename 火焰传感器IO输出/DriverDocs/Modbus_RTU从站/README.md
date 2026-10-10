# Modbus RTU 裸机 RS485 从站（STM32C011参考适配）

更新时间：2026-10-07 08:31:30 +08:00。本次按用户明确批准计划直接入库；硬件状态：待用户验证。

独立新包，既有RTU主站包完整保留。无FreeRTOS、动态分配、任务或消息队列。Device仅依赖标准整数类型；Port不依赖芯片、HAL、RTOS；BSP以官方CMSIS定义访问STM32C011寄存器。不要将原主站BSP与本包同名BSP同时编译。

## 文件和依赖

Device/inc：公共接口和集中配置。Core/Inc、Core/Src：Port及STM32C011 BSP。examples：APP状态寄存器回调、通信总入口和firmware_main最小示例。所有手写实现均已包含；芯片基础依赖为工程内STM32C0 CMSIS头文件、system_stm32c0xx.c和startup_stm32c011xx.s，沿用ST/ARM原许可证。新平台需自行实现BSP契约，不能直接使用C011寄存器。

工程采用本地副本，不引用驱动库绝对路径。通用层及此次C011参考BSP与工程逐字节一致，见来源与一致性清单.txt。examples/APP的通信模块与工程一致；最小App_Main/firmware_main只编排通信。共享工程中的其他传感器调度由各自模块维护，故总编排允许不同。

## 配置与接入

- 默认地址1，9600 bit/s，8N1。dev_modbus_slave_config.h集中配置协议容量、默认地址和波特率；bsp_rs485.h集中配置时序、时钟、优先级与收发器建立时间。当前参考BSP限波特率不高于19200，改变时钟/波特率须重新核对时序。
- 正式引脚表：STM32C011F4P6TR，PA2 USART2_TX、PA3 USART2_RX，均AF1；PA4连接DE与/RE，低为接收/发送高阻，高为发送/关闭接收。PA13/PA14显式配置SWD AF0及上/下拉。
- 保持HSI48/4、SYSCLK/HCLK/APB/TIM14=12MHz；SysTick每1ms。USART2 BASIC不使用FIFO/PRESC/REACK/RTO。TIM14独占，预分频11，1us/计数，冻结比较，不输出GPIO。
- 用户明确选择8N1，每字符10位，t1.5向上取整1563us，t3.5向上取整3646us。这与Modbus串行指南标准11位字符（8E1或无校验8N2）存在差异，主站必须匹配本项目8N1；不宣称标准串行格式全符合。
- 先BSP_Board_Init，再GetModbusSlave获取静态对象，调用对象init(self, address, callback, context)，主循环持续poll(self)。可直接参照examples。正式工程仅编译firmware_main.c提供main，生成main.c、stm32c0xx_it.c及HAL实现保留参考但排除构建；不改.ioc。
- 示例寄存器03/04相同：零基0x0000=0x0100（协议版本），0x0001=0x0001（测量数据未接入），均为只读原始值，无物理量单位，不提供虚构测量值。

## Device契约

```c
typedef DevModbusRegisterResult (*DevModbusReadRegisters)(
    void *context, uint8_t functionCode, uint16_t startAddress,
    uint16_t quantity, uint16_t *values);
DevModbusSlaveStatus (*init)(DevModbusSlave *self, uint8_t slaveAddress,
                            DevModbusReadRegisters readRegisters, void *context);
DevModbusSlaveStatus (*poll)(DevModbusSlave *self);
DevModbusSlave *GetModbusSlave(void);
```

回调一次填写整个连续区间，数量1～125，返回主机字节序uint16_t快照；Port转换为高字节先发。回调快速返回，不阻塞、不分配内存、不递归访问驱动，只在主循环调用。上下文由应用保证生命周期。回调成功0、非法地址2、非法值3、设备失败4分别映射协议异常；其他失败统一异常04。

poll的OK表示本地应答已提交，IDLE表示暂无帧或已静默丢弃，BUSY表示发送/保护间隔/总线活动取消本次应答；负值表示参数、未初始化、IO或超时错误，见头文件。异步提交成功不表示主站已收到。发送期限按长度×10位/9600向上取整毫秒加20ms；超时关闭UART/定时器、PA4拉低、返回TIMEOUT，需重新初始化。示例入口保留故障码供调试器查看并停止后续业务。

## 帧、时序和所有权

静态RX256字节、TX256字节、寄存器暂存125项（250字节）；没有malloc等调用。ISR只收发字节和维护时序，CRC、校验、回调在主循环。RX在RECEIVING由中断写入，READY/HELD期间内容和长度冻结，主循环只读；新总线活动标记冲突并等待静默，不能覆盖借用帧。主循环释放后恢复接收。TX归Port持有，在异步发送及保护间隔期间不再组帧写入。

启动先等待t3.5静默。t1.5结合USART.BUSY检测下一字符是否已经开始，避免把合法间隔中正在接收的字符误丢弃；断帧、硬件错误、溢出重新等待t3.5。PA4先低后配置输出，发送时置高后等待2us（器件最坏900ns使能/退出关断）；TXE写TDR，最后TC确认停止位完成才置低。软件中断延迟会影响实际时序，需要实测核对。主循环应频繁轮询，回调和其他模块不能长时间阻塞；单缓冲区不排队，处理期间非法提前请求会丢弃。

地址1～247、长度、CRC、数量和16位地址范围均校验；03/04合法请求应为8字节，数量1～125。其他地址、广播读取、CRC错误、损坏/溢出帧静默丢弃；合法本机请求返回01非法功能、02非法地址、03非法数量/值/请求长度、04设备失败。功能码最高位置位的异常应答不作为请求处理。

## 来源和验证状态

CRC逐位算法、0xFFFF初值/0xA001多项式及协议常量来源于既有Modbus/RTU主站_RTU_RS485，计数扩展uint16_t以覆盖256字节；协议角色、回调、异步裸机状态机及C011板级实现为本次新增。旧包没有修改。

依据：根目录正式引脚表V1.0；ST DS13866 Rev3表13、RM0490 Rev3 RCC/TIM14/USART/SWD；UMW ADM3485EARZ手册使能、关断及DE//RE真值表；Modbus Application Protocol V1.1b3和Serial Line V1.02。资料页码、PDF哈希和解析状态留在项目附件和同目录解析文件。

正式目标Temperature_and_Vibration_Sensor，ARM Compiler6.9，全量编译/链接/HEX生成通过，0错误0警告。首次最小通信构建Flash4404字节、RAM2472字节；共享工程交付复核（含并行加入的温度/震动模块文件）Flash6800字节、RAM2520字节，符合16KB/6KB。RAM包含启动文件预留1024字节栈、512字节堆空间；预留堆不代表动态分配，最终符号无动态分配调用。库文件静态一致性已核对；库examples未单独作为Keil目标编译，工程构建不替代硬件验证。

未运行软件模拟或单元测试；未烧录或调试实物。硬件状态：待用户验证。验证应覆盖03/04正常读取、数量0/126、跨界/未实现地址、错误功能、CRC错误、其他地址、广播、连续合法请求、断帧、256字节边界/溢出、发送超时故障及PA4最后停止位后释放。详见项目附件通信接入与验证说明.txt。

最终修订复核（2026-10-07 08:33:39 +08:00）：重新初始化会先关闭旧UART/定时器事务并释放方向，再尝试新配置，失败不遗留旧发送占线。正式最终重建Code6488/RO288/RW24/ZI2496，Flash6800/RAM2520字节，0错误0警告；此记录取代前次交付复核资源数值。


## 原始帧主动发送扩展 2026-10-09 16:22:12 +08:00
用户明确要求实施火焰事件主动上报计划，包含Device/Port/BSP主动发送扩展。
DevModbusSlave.init(self, address, NULL, NULL)选择独占原始帧模式；非空回调保持原有Modbus行为。
新增sendRaw(self, data, length)，完整签名DevModbusSlaveStatus (*sendRaw)(DevModbusSlave *self, const uint8_t *data, uint16_t length)。
Port复制原始帧至静态缓冲区，只在poll已确认发送及保护间隔结束后才能覆盖；不添加地址/功能码/CRC。
原始模式poll静默丢弃RX，sendRaw在Modbus模式拒绝调用，必须保持同一硬件独占。
BSP_Rs485_StartSend(const uint8_t *data, uint16_t length)签名不变，增加完整静默IDLE主动发送，保留HELD应答。
参考C011 BSP仅同步此状态条件，仍使用C011 CMSIS、引脚及时钟，未复制F051寄存器内容。
APP层协议由应用维护；火焰工程的65字节协议见该工程DriverDocs/火焰事件主动上报/协议与接入说明_V1.txt。
Fire_Alarm/STM32F051正式目标ARMCC5.06u6 build750：0错误0警告；Code7742/RO374/RW80/ZI2728。
GCC使用BSP/Device替身验证原始发送、在途缓冲稳定、忙、超时、RX丢弃与原Modbus回归，通过。
火焰APP软件场景和Python接收端5项测试通过；软件替身结果不证明真实寄存器或判火阈值有效。
工程Device/Port对应通用内容与库逐字节一致；C011参考BSP未编译，硬件待用户验证。无函数删除或屏蔽。
