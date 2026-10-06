# 压差风速传感器固件

当前版本采用 STM32F103C8T6 内部 HSI 标称 8MHz，关闭 HSE 和 PLL。系统及 APB 总线均不分频。

- USART2：PA2 发送、PA3 接收，9600/8N1，无流控，收到的有效字节原样回传。
- RS485：保留现有每秒电压发送。
- 保留电压采样、数码管、红外及现有执行器控制。
- 集成 CmBacktrace，异常日志通过 USART2 打印；诊断版本标识 HSI8-CMB-20261006。

## 打开与构建

用 Keil 打开 Differential_Pressure_Air_Velocity_Sensor_board/MDK-ARM/Differential_Pressure_Air_Velocity_Sensor_board.uvprojx。

正式目标为 Differential_Pressure_Air_Velocity_Sensor_board，使用 ARMCC 5.06 update 6（build 750）。APP、Device、BSP/Port、FreeRTOS 和 CmBacktrace 均在工程内，构建使用相对路径。

2026-10-06 HSI 恢复后的正式编译、链接：0 错误、0 警告。Flash 占用 31388 字节，静态 RAM 占用 14712 字节。HEX、配套 AXF 和 MAP 位于上述 MDK-ARM 目录的同名输出子目录。

当前固件 SHA-256：

    HEX A456CE9928BC433983C7643CABC328E87637AA470B7E6F27C8A7248A75452518
    AXF 8EDB4CA2A2043E9F55CED015BB39EC9F25F0033B75B0BAB60120E0DB0188C2B3

## 验证状态

当前 HSI 版本尚待用户实板验证。此前 PendSV 中发生的 HardFault 原始原因尚未确认；本次恢复 HSI 不作为故障已修复的结论。AI 未烧录或执行实物测试。

下载新固件后使用本次 AXF 调试，并清理旧版本的绝对地址断点。串口、回溯和采样交接步骤见 移植说明.txt、栈回溯使用说明.txt 和 ADC使用说明.txt。

第三方源码及其许可证保留在相应目录。仓库原有 要求.txt 保留。
