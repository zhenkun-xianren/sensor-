# NEC 红外遥控驱动（设备对象接口维护版）

## 维护版本与验证状态

2026-10-05 按用户批准的红外统一计划，以 `Ultrasonic_Ranging` 工程现有版本维护本包。Device、Port 的对应函数、类型、常量、实现及中文注释与工程副本一致；全部数字及加减键映射保留，没有功能屏蔽。

当前正式工程目标 `Ultrasonic_Ranging_Program` 使用 uVision 5.25、Arm Compiler 5.06 update 6（build 750）完成全量编译、链接及 HEX 生成，0 错误、0 警告。本次只进行了静态核对与该正式工程构建，未运行软件模拟、单元测试或其他软件测试。

**库参考 BSP 未单独编译；工程及库参考硬件均待用户验证。** 工程构建结果不等于库参考包的独立构建或实物验收结果，不沿用历史软件测试证明本次已测试。正式详细更新记录位于上一层 `驱动库更新日志.txt`。

旧 NEC/F103 包的全部六个原文件已在当前项目附件批次 `红外驱动库统一_2026-10-05_15-36-52/修改前备份/库旧NEC` 原字节备份，原路径和 SHA-256 见该批次清单。工程 `DriverReference/IR_NEC_Library` 是旧 F103 来源快照，保留原字节，不作为当前维护版本。其他工程不随本次更新改变。

## 分层与文件

- `Device/inc/dev_ir_remote.h`：芯片、板卡及 RTOS 无关的 `DevIrRemote` 对象、结果码和按键事件。
- `Core/Inc/port_ir_remote.h`、`Core/Src/port_ir_remote.c`：协议容差、键码提示、NEC 状态机、固定队列、Device 方法实现及绑定。
- `Core/Inc/bsp_ir.h`、`Core/Src/bsp_ir.c`：从当前工程复制的 STM32G071RBT6 参考 BSP，PD0/EXTI0 双边沿及 TIM2 1 MHz/32 位计时，提供回调和保存/恢复 PRIMASK 的临界区。
- `Core/Inc/bsp_board.h`、`Core/Src/bsp_board.c`：64 MHz 板级时钟定义与 `BSP_Board_CheckClock()` 只读核对；该函数不会建立或切换时钟。
- `Drivers/CMSIS`：当前工程使用的官方 CMSIS/STM32G071 头文件与许可证，保持原字节；`Device/ST/STM32G0xx/Source/Templates` 提供当前工程的系统源文件和 ARM 启动文件副本。

BSP **只做参考**，不属于跨平台通用 Device/Port。参考接线来自本项目既有软件基线：2026-09-21 网表 IR1=PD0，IRM-3638T 供电 3.3 V。寄存器依据沿用源码中的 DS12232、RM0444 引用，本次没有重新解析手册或验证实板。

## 接口及行为

```c
DevIrRemote *GetIrRemote(void);
/* 以下是 DevIrRemote 的方法，self 必须为上述函数返回的静态对象。 */
DevIrResult (*init)(DevIrRemote *self);
DevIrResult (*read_event)(DevIrRemote *self, IrKeyEvent *event);
DevIrResult (*get_overflow_count)(DevIrRemote *self, uint32_t *count);

void PortIr_DecoderInit(NecDecoder *decoder);
uint8_t PortIr_FeedPulse(NecDecoder *decoder, uint8_t level,
                         uint32_t duration_us, uint32_t end_us, IrKeyEvent *event);
int8_t PortIr_CommandDigit(uint8_t command);
```

`GetIrRemote()` 只绑定方法，不访问硬件。必须显式初始化成功后才能消费事件；初始化调用须串行。`init()` 成功清空队列、协议历史和溢出计数，失败保持未就绪。状态为 `DEV_IR_OK=0`、`DEV_IR_NO_EVENT=1`、`DEV_IR_ERROR_ARGUMENT=-1`、`DEV_IR_ERROR_NOT_READY=-2`、`DEV_IR_ERROR_INIT=-3`。

读取非阻塞；空队列、参数错误及未初始化时不改输出。队列使用 `PORT_IR_QUEUE_SIZE=8` 个槽，实际可保存7条事件；满时丢弃最新事件，溢出计数到 `UINT32_MAX` 后饱和。没有动态内存或自行创建的任务、定时器。

NEC 标准/扩展地址和命令反码校验保留；完整32位数据后必须收到有效末尾低脉冲才输出事件。重复帧只沿用最近有效完整/重复事件后150 ms内的历史，边界150 ms包含在有效范围内；异常帧、丢失捕获或过期会清除可重复历史。微秒时长及结束时间戳使用32位，允许自然回绕；BSP必须正确识别更长的空闲，不能把它截断成短脉冲。

协议输入电平0/1代表已结束脉冲，`BSP_IR_LEVEL_LOST=2` 表示丢失捕获，`BSP_IR_LEVEL_IDLE=3` 表示计数器回绕时的空闲检查。Port中的超时、重复过期及帧判断保留在协议层。

`IrKeyEvent` 包含地址、命令、数字提示、重复标志、原始码及加减提示。驱动只产生事件，不执行显示或执行器动作。当前工程APP每10 ms检查事件，仅数字1至9把 `000.0` 更新为 `000.1` 至 `000.9`；这是工程业务，不是库的固定行为。

## 初始化与调用示例

调用工程先完成显式SWD、板级时钟及基础初始化，并正确维护 `SystemCoreClock`。参考BSP接受当前64 MHz PLL、AHB/APB不分频配置；不得靠修改核对函数掩盖实际时钟不一致。

APP只包含Device头文件，按设备契约检查对象、初始化结果及读取结果。例如以下调用片段仅演示接口，不自动执行或参与库构建：

```c
#include <stddef.h>
#include "dev_ir_remote.h"

/* 在所属模块保存对象并检查每一步结果。 */
DevIrRemote *ir = GetIrRemote(); // 保存本模块使用的红外对象。
IrKeyEvent event;               // 保存本次读取的完整事件。
uint32_t overflow_count;        // 保存累计丢弃事件数量。
DevIrResult result;             // 保存接口调用结果。

if (ir != NULL) {
    result = ir->init(ir);
    if (result == DEV_IR_OK) {
        result = ir->read_event(ir, &event);
        /* DEV_IR_OK时处理event；NO_EVENT时等待；其他状态进入模块故障处理。 */
        result = ir->get_overflow_count(ir, &overflow_count);
        /* 仅DEV_IR_OK时使用overflow_count。 */
    }
}
```

## BSP契约与移植

Port依赖下列BSP接口；移植时签名、失败状态及临界区语义必须保持：

```c
typedef void (*BspIrPulseCallback)(uint8_t level, uint32_t duration_us,
                                   uint32_t end_us);
int BSP_Ir_Init(BspIrPulseCallback callback);
uint32_t BSP_Ir_EnterCritical(void);
void BSP_Ir_ExitCritical(uint32_t saved_mask);
void BSP_Ir_IrqHandler(void);
```

参考BSP只有在初始化核对成功后启用中断；资源占用、时钟、引脚锁定或寄存器核对失败返回非0。不得抢占已启用的UCPD2、TIM2或EXTI0。PD0默认CC下拉按当前实现通过UCPD2_STROBE解除；该动作同时影响PD2的CC下拉，须核对同板其他模块。

参考源定义 `EXTI0_1_IRQHandler` 和 `TIM2_IRQHandler`，TIM2回绕中断记录长空闲，EXTI仅处理所属线路挂起位。当前源适用于只有EXTI0使用者的共享向量；目标工程若另用EXTI1，必须合并向量分发，不能同时链接重复向量或忽略另一线路。回绕与边沿处理使用相同优先级，避免共享时间状态互相抢占。

复制到目标工程时，加入Device、Port及目标BSP/板级代码，使用工程内相对路径。G071参考所需宏为 `STM32G071xx`；头搜索路径包含 `Device/inc`、`Core/Inc`、`Drivers/CMSIS/Include`、`Drivers/CMSIS/Device/ST/STM32G0xx/Include`。目标工程须提供唯一的启动文件及系统源，定义 `SystemCoreClock`；本包附带模板可用于该依赖，已有同版本实现时不要重复编译。保留CMSIS和ST的许可证、原始版权说明。

参考包不提供板级启动主程序、64MHz时钟建立、FreeRTOS或应用任务，也未提供独立Keil工程。使用其他芯片或板卡时重新适配BSP，核对时钟、输入电平、捕获丢失、长空闲、资源归属及共享IRQ；不要复制本板引脚、UCPD配置或启动流程。

## 旧接口迁移

新版不提供兼容包装；以下三个入口已按用户明确授权移除：

- `void DevIrRemote_Init(void)` → `GetIrRemote()` 后调用 `ir->init(ir)`，检查结果。
- `uint8_t DevIrRemote_ReadEvent(IrKeyEvent *event)` → `ir->read_event(ir, event)`；注意新版**0是成功，1是无事件**，禁止沿用旧版非零即成功的判断。
- `void PortIr_OnPulseIsr(uint8_t level, uint16_t duration_us)` → BSP保存初始化传入的回调并提供真实的32位微秒间隔及结束时间戳，通知Port；不再直接调用旧公共入口。

旧 `PortIr_FeedPulse(decoder, level, uint16_t_duration, event)` 改为五参数接口，调用方须提供真实时长及结束时间戳。不能仅补零、拼接被截断的16位间隔或忽略回绕来模拟正确计时。库旧BSP为F103/PA8/TIM3；新参考为G071/PD0/TIM2，不能混用。需要维护旧产品时使用已备份旧包或另立维护版本，不用新版文件部分覆盖旧接口工程。
