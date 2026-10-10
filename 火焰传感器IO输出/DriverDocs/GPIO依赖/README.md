# GPIO初始化契约与STM32F103C8T6五路执行器参考

## 维护版本与范围

2026-10-05按用户确认计划维护，与Ultrasonic_Ranging工程的GPIO Device/Port、共用DevStatus保持一致。保留GetGpio、DevGpio.init及DevStatus PortGpio_Init(void)。对象初始化只接受GetGpio返回的对象，非法对象返回DEV_ARGUMENT_ERROR；板级失败返回DEV_IO_ERROR。

本包F103参考仅配置PA1光耦、PA5继电器、PA6有源蜂鸣器、PB15报警灯、PB14电源灯。PA1低有效，其他四路高有效；默认PA1高、其余低，全部逻辑关闭。保持来源板卡2MHz推挽输出，不照搬G071的5V开漏灯和UCPD2配置，不声明这些接线已在实物验证。

## 文件与依赖

- Device/inc/dev_gpio.h、Core/Inc/port_gpio.h、Core/Src/port_gpio.c为通用接口；共用/Device/inc/dev_common.h提供状态定义。
- 本包Core/Inc/bsp_gpio.h、bsp_gpio_board.h和Core/Src/bsp_gpio.c为独立五路F103参考；执行器新版参考必须配套使用。
- Drivers/CMSIS/Include和Drivers/CMSIS/Device/ST/STM32F1xx/Include提供官方原字节CMSIS头文件，两份许可证一并保留。引用这些工程内相对目录，禁止依赖来源工程绝对路径。
- 参考只提供GPIO/执行器适配，不包含完整固件启动或系统时钟实现。移植工程须在启动阶段先按官方手册显式配置并保留SWD、关闭JTAG，核对调试器SW/SWD设置；本包不修改PA13/PA14、AFIO调试配置、锁定位或选项字节。未经核对不能宣称完整F103启动满足SWD要求。

## 两种F103实现择一

原平台适配/STM32F103C8T6/Core中的整板bsp_gpio.h/bsp_gpio.c原字节保留，继续作为旧整板来源参考，包含其他功能引脚配置。它缺少新版执行器所需的检查/关闭契约，不能直接配套本次新版执行器Port。

使用新版五路包时，仅编译本包bsp_gpio.c并选择本包Core/Inc，禁止同时编译旧共享整板bsp_gpio.c，禁止混用同名bsp_gpio.h。需要旧整板方案时使用该方案的成套源码，或依据目标板手册另行适配新版契约；本次不修改其他驱动。

## 初始化与调用

先完成目标板启动，再通过DevGpio *gpio = GetGpio()取得对象，检查对象和init方法，调用gpio->init(gpio)，只在DEV_OK时初始化GetActuator返回的执行器对象。APP通过Device契约调用；PortGpio_Init保留为底层兼容入口，APP不跨层调用。

GPIO初始化先预置关闭锁存再启用输出，仅对所属字段读改写，保存/恢复PRIMASK。执行器控制前检查时钟及配置；失败尽力关闭可访问锁存，不保证真实负载已关闭。GPIO重复初始化会关闭全部执行器，正常运行期间应避免重复调用。

## 依据与验证状态

RM0008 Rev21第171至173页确认CRL/CRH四位字段、CNF=00与MODE=10的2MHz推挽输出、ODR读回及BSRR原子置位/复位；DS5319 Rev20确认F103x8/xB引脚资料。接线/极性沿用库来源，不借用其他项目功能要求。

工程G071正式Compiler5.06 update6编译、链接及HEX/AXF生成通过，0错误0警告；通用层静态核对。F103参考未单独编译，软件测试未运行，硬件待用户验证，未标记任何平台硬件已验证。实际更新日志位于GPIO/驱动库更新日志.txt。
