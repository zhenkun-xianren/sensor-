#ifndef DEV_DISPLAY_H
#define DEV_DISPLAY_H

#include <stdint.h>

/**
 * @brief 四位数码管的显示位置，DIG1 为最左侧一位。
 */
typedef enum {
    DEV_DISPLAY_DIG1 = 0U, /**< 最左侧数码位。 */
    DEV_DISPLAY_DIG2,      /**< 从左向右第二位。 */
    DEV_DISPLAY_DIG3,      /**< 从左向右第三位。 */
    DEV_DISPLAY_DIG4       /**< 最右侧数码位。 */
} DevDisplayPosition;

/**
 * @brief 数码管小数点的显示状态。
 */
typedef enum {
    DEV_DISPLAY_DECIMAL_POINT_OFF = 0U, /**< 关闭小数点。 */
    DEV_DISPLAY_DECIMAL_POINT_ON        /**< 点亮小数点。 */
} DevDisplayDecimalPoint;

/**
 * @brief 提供四位数码管初始化、扫描和内容显示能力的设备接口。
 */
typedef struct DevDisplay {
    /**
     * @brief 初始化数码管显示设备。
     * @param self 接收初始化请求的数码管设备对象。
     */
    void (*init)(struct DevDisplay *self);

    /**
     * @brief 扫描当前数码位并切换到下一位。
     * @param self 接收扫描请求的数码管设备对象。
     */
    void (*scanStep)(struct DevDisplay *self);

    /**
     * @brief 清空四个数码位的显示内容。
     * @param self 接收清屏请求的数码管设备对象。
     */
    void (*clear)(struct DevDisplay *self);

    /**
     * @brief 在指定数码位显示一个十进制数字及可选小数点。
     * @param self 接收显示请求的数码管设备对象。
     * @param position 显示位置，取 DEV_DISPLAY_DIG1 至 DEV_DISPLAY_DIG4。
     * @param digit 要显示的数字，范围为 0 至 9；超出范围时显示空白。
     * @param decimal_point 小数点状态，取 DEV_DISPLAY_DECIMAL_POINT_OFF 或 DEV_DISPLAY_DECIMAL_POINT_ON。
     */
    void (*showDigit)(struct DevDisplay *self, DevDisplayPosition position,
                      uint8_t digit, DevDisplayDecimalPoint decimal_point);

    /**
     * @brief 在四位数码管显示无小数点的整数，空缺高位补零。
     * @param self 接收显示请求的数码管设备对象。
     * @param value 要显示的整数，范围为 0 至 9999；超出范围时显示范围错误标记。
     */
    void (*showInteger4)(struct DevDisplay *self, uint16_t value);

    /**
     * @brief 在四位数码管显示指定小数位数的定点数。
     * @param self 接收显示请求的数码管设备对象。
     * @param scaled_value 按小数位放大后的整数值，范围为 0 至 9999；例如 1234 配合 1 位小数显示 123.4。
     * @param decimal_places 小数位数，范围为 0 至 3；超出范围时显示范围错误标记。
     */
    void (*showFixedPoint)(struct DevDisplay *self, uint16_t scaled_value,
                           uint8_t decimal_places);

    /**
     * @brief 显示错误标记及错误码的末两位数字。
     * @param self 接收显示请求的数码管设备对象。
     * @param code 错误码；显示格式为左侧空白、E、十位、个位。
     */
    void (*showError)(struct DevDisplay *self, uint8_t code);

    /**
     * @brief 点亮四个数码位的所有段和小数点。
     * @param self 接收显示请求的数码管设备对象。
     */
    void (*showAll)(struct DevDisplay *self);
} DevDisplay;

/**
 * @brief 获取数码管显示设备接口。
 * @return 指向静态数码管设备接口的指针。
 */
DevDisplay *GetDisplay(void);

#endif
