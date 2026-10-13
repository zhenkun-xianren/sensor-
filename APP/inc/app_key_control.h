#ifndef APP_KEY_CONTROL_H
#define APP_KEY_CONTROL_H

#include <stdbool.h>
#include "dev_ir_remote.h"

/**
 * @brief 查询数字键显示业务所需的显示模块是否就绪。
 * @return 显示可用返回true，未启动或已故障返回false。
 */
bool AppKeyControl_CanHandleEvent(void);

/**
 * @brief 将数字键1至9映射为显示数值，数字0及其他键保持原显示。
 * @param event 当前红外事件的非空指针，仅在本次调用期间使用。
 * @return 处理或忽略事件成功返回true，参数无效或显示提交失败返回false。
 */
bool AppKeyControl_HandleEvent(const IrKeyEvent *event);

#endif
