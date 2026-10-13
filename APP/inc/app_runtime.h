#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H
#include <stdint.h>

/** @brief 运行框架的软件诊断快照，不包含板级外设信息。 */
typedef struct {
    uint32_t wake_count; // 已消费的软件定时器通知总数，超过 UINT32_MAX 后自然回绕。
    uint32_t timer_count; // 软件定时器回调总数，超过 UINT32_MAX 后自然回绕。
    uint32_t task_stack_free_words; // 运行任务历史最小剩余栈空间，单位为 32 位字。
    uint32_t heap_free_bytes; // 最近一次内核堆余量，单位为字节；首次动态分配前 heap_4 报告 0。
} AppRuntimeSnapshot;

/**
 * @brief 在调度器启动前创建静态任务及周期软件定时器，禁止重复启动。
 * @return 0 表示创建及定时器启动请求成功；非 0 表示重复启动或创建/入队失败。
 */
int AppRuntime_Start(void);

/**
 * @brief 获取一致的软件诊断快照，仅供任务上下文使用。
 * @param output 非空输出对象，用于接收计数、栈字数和堆字节数。
 * @return 0 表示已复制；非 0 表示输出为空或模块尚未成功启动。
 */
int AppRuntime_GetSnapshot(AppRuntimeSnapshot *output);

#endif
