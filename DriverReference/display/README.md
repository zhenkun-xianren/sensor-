# FJ5461AH 四位共阴数码管统一 Device、Port

本版本依据2026-10-05用户批准的接口统一计划维护。超声波工程使用D:\Keil\MDK下Compiler 5.06 update 6（build 750）正式重建、链接并生成HEX，0错误、0警告；通用层由工程原样复制入库。此前另一套D:\Keil\V5.4安装的构建因未配置Compiler 5中止，该历史日志不代表本机没有Compiler 5。

## 通用层与接口

`include/dev_display.h`、`include/port_display.h`、`src/port_display.c`与超声波工程当前对应文件逐字节一致。通用层依赖标准C类型和BSP函数契约，不依赖STM32芯片头文件、寄存器或FreeRTOS；工程使用本地副本，不通过库绝对路径引用代码。

定点显示只保留`showFixedPoint(self, scaled_value, decimal_places, mode)`。`DEV_DISPLAY_LEADING_ZERO_SUPPRESS`抑制整数前导零，`DEV_DISPLAY_LEADING_ZERO_PAD`补齐整数区域；例如数值1、小数位1分别显示左侧空白的`0.1`和`000.1`。整数接口复用同一编码和提交流程，使用零小数位和补零模式。

初始化、扫描、清屏、单数字、整数、定点数、错误码、全段显示及停止方法均返回`DevDisplayStatus`。无效对象或显示模式、未初始化、数值越界和板级失败分别返回明确状态；无效模式保持原显示，数值或小数位越界提交E04。Device方法只能通过GetDisplay返回的对象调用。

Port维护当前帧、待提交帧及扫描位置；最近提交帧只在DIG1扫描边界生效，同一四位周期不混用新旧帧。`clear`提交空白帧，`stop`立即关闭输出并阻止扫描点亮；合法内容更新重新启用扫描。

## BSP配套与初始化

新版Port只依赖`BSP_Display_Init`、`BSP_Display_EnterCritical`、`BSP_Display_ExitCritical`、`BSP_Display_Blank`和`BSP_Display_Output`。BSP不再维护第二份显示帧或扫描状态；不保留旧SetFrame、SetDigit、ScanStep接口。

STM32F103参考沿用原包GPIOA/GPIOB映射、FJ5461AH与74AHCT245高有效段选及位选。调用方须预先开启所属GPIO时钟，配置所属引脚及PA15普通GPIO功能，并按目标芯片规则保留SWD、关闭JTAG。参考不配置系统时钟或整板GPIO，不检查初始化配置或实物输出。参考引脚只适用于其来源板卡，不能用于推断其他板卡连接。

1. 工程完成BSP所需的时钟、调试及GPIO前置配置。
2. 获取设备并检查`display->init(display)`返回状态。
3. 用一个应用周期源调用`scanStep`，按实际刷新要求选择周期；超声波工程使用1ms。
4. APP只通过Device更新内容，调度器、任务及定时器由工程管理。

```c
DevDisplay *display = GetDisplay(); // 获取显示设备契约对象。
DevDisplayStatus result = display->init(display); // 保存初始化结果。
if (result == DEV_DISPLAY_OK) {
    result = display->showFixedPoint(display, 1U, 1U, DEV_DISPLAY_LEADING_ZERO_PAD);
}
/* 应用检查result；周期源调用scanStep并处理其返回结果。 */
```

## 验证与版本边界

- 工程：正式Compiler 5.06 update 6（build 750）重建、链接及HEX生成通过，0错误、0警告；目标Ultrasonic_Ranging_Program。
- 软件测试：按用户明确要求未运行；编译结果不能替代软件测试结果。
- STM32F103 BSP参考：参考实现未编译、硬件未验证；不以工程结果证明该参考可用。
- STM32G071工程BSP：保留工程自身适配，硬件待用户验证；不会复制为已验证平台适配。
- 正式库：工程编译及链接通过后原样同步三个通用文件，并更新配套BSP参考和说明；实际更新记入库根目录驱动库更新日志.txt。BSP参考未单独编译，不使用工程结果替代其验证。
- 源快照、历史来源清单及另一份历史数码管归档不改写；本候选的说明不代表历史版本已经复核。
