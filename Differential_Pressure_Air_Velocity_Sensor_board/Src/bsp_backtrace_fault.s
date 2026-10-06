; @brief ARMCC5 异常入口：不压栈保存原 LR/MSP，使用独立诊断栈。
; r0: 原始 EXC_RETURN；r1: 异常入口 MSP；PSP 保持原现场供 CmBacktrace 读取。
; 本工程入口替代上游可选 cmb_fault.s，避免重复定义和故障栈上再次打印。

Backtrace_Stack_Size EQU 0x800
                AREA BACKTRACE_STACK, NOINIT, READWRITE, ALIGN=3
                EXPORT g_backtrace_stack_start
                EXPORT g_backtrace_stack_end
g_backtrace_stack_start
                SPACE Backtrace_Stack_Size
g_backtrace_stack_end

                AREA |.text|, CODE, READONLY, ALIGN=2
                THUMB
                REQUIRE8
                PRESERVE8
                IMPORT BSP_Backtrace_Fault
                IMPORT g_backtrace_entry_active
                EXPORT HardFault_Handler
                EXPORT MemManage_Handler
                EXPORT BusFault_Handler
                EXPORT UsageFault_Handler
                EXPORT NMI_Handler

; @brief 五种异常共用入口；使用 r2/r3 检查重入，原始 R0-R3 已在异常帧中。
HardFault_Handler
MemManage_Handler
BusFault_Handler
UsageFault_Handler
NMI_Handler     PROC
                CPSID i
                MOV r0, lr
                MRS r1, MSP
                LDR r2, =g_backtrace_entry_active
                LDR r3, [r2]
                CMP r3, #0
                BNE Backtrace_Hold
                MOVS r3, #1
                STR r3, [r2]
                LDR r2, =g_backtrace_stack_end
                MSR MSP, r2
                ISB
                B BSP_Backtrace_Fault
Backtrace_Hold
                NOP
                B Backtrace_Hold
                ENDP
                ALIGN
                END
