#ifndef APP_IR_REMOTE_H
#define APP_IR_REMOTE_H

#include <stdbool.h>
#include "dev_ir_remote.h"

/** @brief 红外接收模块启动结果。 */
typedef enum {
    APP_IR_REMOTE_OK = 0, // 设备、事件任务及轮询定时器均已准备完成。
    APP_IR_REMOTE_FAILED = -1 // 参数、设备初始化或静态对象启动失败。
} AppIrRemoteResult;

/**
 * @brief 处理一条已解码的红外事件，由独立业务模块实现。
 * @param event 仅在回调期间有效的非空事件指针，不得保存该指针供后续使用。
 * @return 事件已处理或无需处理返回true，业务失败返回false并锁存处理故障。
 */
typedef bool (*AppIrRemoteEventHandler)(const IrKeyEvent *event);

/**
 * @brief 查询业务依赖是否就绪，在每次轮询读取队列前调用。
 * @return 可以处理事件返回true，依赖未就绪或故障返回false并跳过本次读取。
 */
typedef bool (*AppIrRemoteReadyHandler)(void);

/**
 * @brief 初始化红外设备并创建事件任务及十毫秒静态轮询定时器。
 * @param event_handler 非空业务事件回调，在红外事件任务上下文执行。
 * @param ready_handler 非空业务就绪回调，在红外事件任务上下文执行。
 * @return 成功返回APP_IR_REMOTE_OK，重复启动、参数或资源失败返回APP_IR_REMOTE_FAILED。
 */
AppIrRemoteResult AppIrRemote_Start(AppIrRemoteEventHandler event_handler,
                                  AppIrRemoteReadyHandler ready_handler);

#endif
