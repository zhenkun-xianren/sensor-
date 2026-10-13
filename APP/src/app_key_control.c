#include <stddef.h>

#include "app_display.h"
#include "app_key_control.h"

#define APP_KEY_CONTROL_FIRST_DIGIT 1
#define APP_KEY_CONTROL_LAST_DIGIT 9

/**
 * @brief 查询数字键显示业务的显示依赖是否就绪，故障时暂停红外队列读取。
 * @return 显示模块已启动且未故障返回true，否则返回false。
 */
bool AppKeyControl_CanHandleEvent(void)
{
    return AppDisplay_IsReady();
}

/**
 * @brief 将数字键1至9映射为000.1至000.9，数字0及其他键保持原显示。
 * @param event 当前红外事件的非空指针，不保存该指针。
 * @return 显示提交成功或忽略无关键返回true，参数无效或显示提交失败返回false。
 */
bool AppKeyControl_HandleEvent(const IrKeyEvent *event)
{
    if (event == NULL) {
        return false;
    }
    if (event->digit < APP_KEY_CONTROL_FIRST_DIGIT ||
        event->digit > APP_KEY_CONTROL_LAST_DIGIT) {
        return true;
    }
    return AppDisplay_ShowValue((uint16_t)event->digit) == DEV_DISPLAY_OK;
}
