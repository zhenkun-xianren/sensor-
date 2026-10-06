# TTL UART 原始字节驱动（工程内新实现）

本驱动用于当前 STM32F103C8T6 工程的 USART2，PA2 发送、PA3 接收，9600/8N1、无流控。GPIO 连接来自当前工作区正式引脚表 V1.0；不得用本参考说明替代其他项目的正式引脚表。

## 源码与依赖

- 通用契约：`../../Device/inc/dev_uart.h`。
- Port：内层板级工程 `Inc/port_uart.h`、`Src/port_uart.c`。
- 本工程 BSP：内层板级工程 `Inc/bsp_uart.h`、`Src/bsp_uart.c`。
- 依赖：工程内已有 `bsp_byte_ring.h/.c`、`bsp_board.h/.c`、STM32F103 CMSIS 头文件及启动向量。
- APP 回传示例：`../../APP/inc/app_uart_echo.h`、`../../APP/src/app_uart_echo.c`。

所有源码在本工程内，通过相对路径加入正式 Keil 目标，不依赖外部驱动库源码。本目录仅保存驱动说明，实际维护源码为上述正式工程文件，不维护第二份通用实现。

## 契约与调用

`GetUart()` 返回唯一静态 `DevUart`；通过 `dev->init(dev)` 初始化，再通过 `dev->readByte(dev, &byte)` 和 `dev->writeByte(dev, byte)` 处理原始字节。运行期由一个任务调用，禁止从中断上下文调用这些方法；初始化仅在通信开始前进行，会丢弃已有收发队列内容。`GetUart()` 本身不清空队列。

`DEV_UART_OK` 表示初始化成功、读出一个字节或成功进入发送队列；发送时不表示最后停止位完成。`EMPTY` 表示暂无接收字节，`BUSY` 表示发送队列满且字节没有入队。错误包括参数、未初始化、接收错误通知和板级 I/O 配置异常。只有读取返回 `OK` 才更新输出参数。

收发分别使用独立的 128 字节环形缓冲区，有效容量各为 127 字节。BSP 短临界区保存并恢复 PRIMASK；中断只执行收发和统计，无 RTOS 调用或硬件轮询。发送队列满时 APP 保留当前字节后续重试；接收队列满时丢弃新字节并记录，有限队列不承诺过载时无丢字节。接收错误通知合并报告，具体错误事件分别累计；已排队有效字节不清空。

初始化顺序：现有调试接口与板级时钟/时基准备 → APP 初始化串口 → 静态回传任务创建 → 调度器启动。任务栈为 256 个 StackType_t 字，优先级 1，每轮最多转交 64 字节后阻塞 1ms。不添加换行、文本终止符或 CRC，不解析 Modbus。

## 寄存器依据与移植

RM0008 Rev21：GPIO 配置章节；p184 的 SWJ_CFG 读值限制；p186 的 USART2 默认映射；p798~799、820 的波特率计算和 BRR；p818~819 的 RXNE、TXE 和 SR/DR 清标志顺序；p821~823 的 8N1 和异步模式。USART2 从 PCLK1 推导 BRR，本工程 PCLK1=8MHz、BRR=833，名义约 9603.84bps。当前工程已切换至X3外部8MHz无源晶振HSE，系统和总线频率保持8MHz；该新时钟配置待用户实板验证。

其他平台复用 Device/Port 时，应按其正式引脚表、板级时钟及官方手册适配 BSP、启动向量和中断优先级，不照搬本工程引脚或 BRR。复制完整源码与依赖进目标工程，不通过外部绝对路径引用。通用层无额外功能屏蔽。

## 授权和验证

用户于本次会话明确同意新增 UART 驱动，并要求实施 PA2/PA3 回传方案。批准的驱动函数为 `GetUart`、`uart_init`、`uart_read_byte`、`uart_write_byte`、`BSP_Uart_Init`、`BSP_Uart_ReadByte`、`BSP_Uart_WriteByte`、`BSP_Uart_IrqHandler`、`USART2_IRQHandler`，其完整签名见实际头文件和定义。

UART入库时的正式目标使用 ARMCC 5.06 update 6 (build 750) 编译、链接及 HEX 生成成功，0 错误、0 警告。软件测试未运行（未明确要求），AI未烧录或操作实物。用户随后反馈“程序正常放到驱动库”，据此确认当时HSI配置下目标板PA2/PA3回传正常并完成入库；详细测试覆盖未提供，不扩大为全部错误路径或其他平台通过。统一驱动包位于 `UART/TTL_UART_字节收发_STM32F103C8T6_USART2_PA2_PA3`，入库时九份源码/头文件与对应来源原字节一致；正式日志位于UART分类的驱动库更新日志.txt。当前工程仅板级头/时钟检查改为HSE，Device/Port、UART BSP和环形队列仍与库一致；库保留已入库的HSI平台参考及验证记录。库参考未独立构建。
