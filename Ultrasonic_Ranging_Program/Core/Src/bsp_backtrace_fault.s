; @brief ARMCC5/Cortex-M0+故障入口；不在原栈上压栈或调用C。
; 独立诊断栈为2KB、8字节对齐，不改变生成启动文件中的STACK。
Backtrace_Stack_Size EQU 0x800
Backtrace_Entered    EQU 1
Control_SPSEL_Mask  EQU 2

                AREA BACKTRACE_STACK, NOINIT, READWRITE, ALIGN=3
                EXPORT g_backtrace_stack_begin
                EXPORT g_backtrace_stack_end
g_backtrace_stack_begin
                SPACE Backtrace_Stack_Size
g_backtrace_stack_end

                AREA |.text|, CODE, READONLY, ALIGN=2
                THUMB
                REQUIRE8
                PRESERVE8
                IMPORT g_backtrace_entry_active
                IMPORT BSP_Backtrace_Fault
                IMPORT BSP_Backtrace_Stop
                IMPORT BSP_Backtrace_Reentry
                EXPORT HardFault_Handler
                EXPORT NMI_Handler
                EXPORT BSP_Fault_Stop

; @brief r0原LR、r1原MSP、r2原PSP、r3异常号；r4/r5仅作临时量。
HardFault_Handler
NMI_Handler     PROC
                CPSID i
                MOV r0, lr
                MRS r1, MSP
                MRS r2, PSP
                MRS r3, IPSR
                LDR r4, =g_backtrace_entry_active
                LDR r5, [r4]
                CMP r5, #0
                BNE Backtrace_Reentry
                MOVS r5, #Backtrace_Entered
                STR r5, [r4]
                LDR r4, =g_backtrace_stack_end
                MSR MSP, r4
                ISB
                LDR r4, =BSP_Backtrace_Fault
                BX r4
                ENDP

; @brief 保留r0原因/r1文件/r2行号，r3捕获当前有效SP；永久停止不返回。
BSP_Fault_Stop  PROC
                CPSID i
                MOV r3, sp
                LDR r4, =g_backtrace_entry_active
                LDR r5, [r4]
                CMP r5, #0
                BNE Backtrace_Reentry
                MOVS r5, #Backtrace_Entered
                STR r5, [r4]
                LDR r4, =g_backtrace_stack_end
                MSR MSP, r4
                ; Thread模式原来可能使用PSP，改用诊断MSP；PSP原值保持。
                MRS r4, CONTROL
                MOVS r5, #Control_SPSEL_Mask
                BICS r4, r5
                MSR CONTROL, r4
                ISB
                LDR r4, =BSP_Backtrace_Stop
                BX r4
                ENDP

; @brief NMI可抢占诊断；重新建立栈顶，仅调用不打印的最小关闭函数。
Backtrace_Reentry PROC
                LDR r4, =g_backtrace_stack_end
                MSR MSP, r4
                MRS r4, CONTROL
                MOVS r5, #Control_SPSEL_Mask
                BICS r4, r5
                MSR CONTROL, r4
                ISB
                LDR r4, =BSP_Backtrace_Reentry
                BX r4
                ENDP
                ALIGN
                END
