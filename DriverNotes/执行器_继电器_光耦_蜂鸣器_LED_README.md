# 继电器、光耦、有源蜂鸣器与指示灯统一执行器

## 维护版本与功能

2026-10-05按用户批准的统一计划维护。Device/Port及共用状态定义与Ultrasonic_Ranging当前工程一致；保留继电器、隔离输出、有源蜂鸣器和两灯控制能力，新增状态查询。库仅维护F103板级参考，工程继续使用G071适配。

## 接口与兼容

GetActuator返回唯一静态对象；init、setRelay、setOptocoupler、setBuzzer、setIndicatorLeds均返回DevStatus，首参数为设备对象，新增getState(self, output)。开关零关闭、非零开启。错误区分非法对象/参数DEV_ARGUMENT_ERROR、未初始化DEV_NOT_READY、板级失败DEV_IO_ERROR。

DevActuatorState五字段relay/optocoupler/buzzer/warningLed/powerLed均为0关闭或1开启，来自ODR锁存的逻辑转换，不采集机械触点、引脚实际电压或负载反馈；查询失败不修改输出对象。

旧Device方法由void升级为DevStatus，原调用源码应使用新版头文件重新编译并检查结果；不承诺二进制兼容。保留旧Port私有包装函数名称，增强其返回状态和检查。旧BSP_Actuator_SetRelay、SetOptocoupler、SetBuzzer、SetIndicatorLeds四个void入口保留原签名，内部转调新版电平接口；它们不报告失败，应优先使用Device契约。BSP_Actuator_Init由void升级为int，返回0成功/-1失败。

新版BSP_Actuator_Set(channel, high)、BSP_Actuator_SetIndicatorLevels(warning_high, power_high)接收0/1原始电平，由Port完成有效电平转换；BSP_Actuator_Get返回原始电平快照。不要将逻辑enabled与原始high参数混用。

## F103映射与实现

PA5继电器高有效，PA1光耦现场输出与GPIO反相，PA6蜂鸣器高有效；PB15报警灯、PB14电源灯高有效。保留来源2MHz推挽输出。关闭时PA1高、其他四路低；init包括两灯全部关闭。两灯同属GPIOB，通过一次BSRR写入成对更新；G071工程跨GPIOC/GPIOD，两次写入处于同一短临界区，各平台差异由BSP承担。

BSP检查配置和ODR结果、保存恢复PRIMASK、板级失败尽力关闭全部可访问输出锁存，并保留原错误。无闪烁、PWM、阈值或红外动作绑定；数码管位选和通信方向属于各自驱动。

## 文件、依赖与复制

完整复制本包Device/Port/BSP、GPIO/STM32F103C8T6内新版五路BSP和官方CMSIS头文件/许可证、共用/Device/inc/dev_common.h到目标工程。设置目标工程内相对搜索路径；不要通过库绝对路径编译。F103依赖选择本次GPIO包Core/Inc，禁止同时编译或混入平台适配/STM32F103C8T6的旧共享整板bsp_gpio实现。使用其他MCU时重新适配BSP，不把F103引脚当作通用接线。

## 初始化示例

```c
DevGpio *gpio = GetGpio();
DevActuator *actuator = GetActuator();
/* 检查对象和方法表后，依次检查GPIO与执行器初始化结果。 */
if (gpio != 0 && gpio->init != 0 && actuator != 0 &&
    actuator->init != 0 && actuator->setIndicatorLeds != 0 &&
    gpio->init(gpio) == DEV_OK && actuator->init(actuator) == DEV_OK) {
    DevStatus result = actuator->setIndicatorLeds(actuator, 0U, 1U);
    /* 检查result；业务默认状态由APP决定。 */
}
```

先完成目标板基础启动及显式SWD配置，再初始化GPIO和执行器。参考不提供完整F103启动工程；GPIO包说明中列出SWD与旧整板依赖边界。

## 验证

寄存器依据RM0008 Rev21 GPIO章节第171至173页及官方F103 CMSIS定义；引脚资料DS5319 Rev20。通用层与工程逐函数及字节核对，G071正式Compiler5.06 update6构建0错误0警告并生成HEX/AXF。软件测试未运行，F103参考未单独编译，硬件待用户验证，不使用工程结果证明F103编译或硬件状态。

正式维护记录位于执行器/驱动库更新日志.txt；修改前完整备份、来源、差异及PDF访问记录保存于本任务AI附件批次。
