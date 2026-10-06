#ifndef DEV_DISPLAY_H
#define DEV_DISPLAY_H

#include <stdint.h>

/** @brief 数码管接口执行状态；所有操作均同步返回，不存在等待状态。 */
typedef enum {
    DEV_DISPLAY_OK = 0, /**< 操作完成或显示帧已提交，等待外部扫描调用输出。 */
    DEV_DISPLAY_INVALID_ARGUMENT = -1, /**< 对象、位置、数字或枚举参数无效，保持原显示。 */
    DEV_DISPLAY_NOT_INITIALIZED = -2, /**< 尚未初始化或最近一次初始化失败。 */
    DEV_DISPLAY_OUT_OF_RANGE = -3, /**< 数值超过9999或小数位超过3，已提交E04帧。 */
    DEV_DISPLAY_HARDWARE_ERROR = -4 /**< 板级初始化或输出失败，停止依赖该硬件。 */
} DevDisplayStatus;

/** @brief 从左到右的四个数码位，与FJ5461AH的DIG1至DIG4一致。 */
typedef enum {
    DEV_DISPLAY_DIG1 = 0, /**< 最左侧数码位。 */
    DEV_DISPLAY_DIG2, /**< 左起第二位。 */
    DEV_DISPLAY_DIG3, /**< 左起第三位。 */
    DEV_DISPLAY_DIG4 /**< 最右侧数码位。 */
} DevDisplayPosition;

/** @brief 单个数码位的小数点显示状态。 */
typedef enum {
    DEV_DISPLAY_DECIMAL_POINT_OFF = 0, /**< 小数点关闭。 */
    DEV_DISPLAY_DECIMAL_POINT_ON = 1 /**< 小数点点亮。 */
} DevDisplayDecimalPoint;

/** @brief 定点数整数部分的前导零显示模式。 */
typedef enum {
    DEV_DISPLAY_LEADING_ZERO_SUPPRESS = 0, /**< 整数有效数字左侧显示空白，至少保留个位。 */
    DEV_DISPLAY_LEADING_ZERO_PAD = 1 /**< 整数有效数字左侧补零，填满四位显示区域。 */
} DevDisplayLeadingZeroMode;

/**
 * @brief 四位数码管设备契约；不暴露芯片、引脚或操作系统。
 * @details 内容更新在下一次DIG1扫描时生效，连续提交只保留最新帧。
 * 所有接口供任务或调度器启动前调用，不在中断中调用；扫描由一个外部调用源驱动。
 * 停止后内容更新重新启用扫描，清屏只清内容，stop立即关闭物理输出。
 */
typedef struct DevDisplay {
    /**
     * @brief 初始化或重新初始化设备并立即关闭输出，清空显示缓冲。
     * @param self GetDisplay返回的唯一设备对象。
     * @return 成功返回OK，对象无效返回INVALID_ARGUMENT，板级初始化失败返回HARDWARE_ERROR。
     */
    DevDisplayStatus (*init)(struct DevDisplay *self); // 初始化设备的方法。
    /**
     * @brief 刷新下一位；建议每1毫秒由一个外部周期源调用，停止后调用不点亮输出。
     * @param self GetDisplay返回的设备对象。
     * @return 成功或停止返回OK，对象无效返回INVALID_ARGUMENT，未初始化返回NOT_INITIALIZED，输出失败返回HARDWARE_ERROR。
     */
    DevDisplayStatus (*scanStep)(struct DevDisplay *self); // 单步扫描的方法。
    /**
     * @brief 提交全空白帧，在下一次四位扫描边界生效。
     * @param self GetDisplay返回的设备对象。
     * @return 成功返回OK，对象无效返回INVALID_ARGUMENT，未初始化返回NOT_INITIALIZED。
     */
    DevDisplayStatus (*clear)(struct DevDisplay *self); // 清空显示内容的方法。
    /**
     * @brief 更新一个数码位，其他位保留最近提交的内容。
     * @param self GetDisplay返回的设备对象。
     * @param position 位置，DIG1至DIG4。
     * @param digit 数字，0至9；超出范围保持原显示。
     * @param decimal_point 小数点，OFF或ON；其他值保持原显示。
     * @return 成功返回OK，参数无效返回INVALID_ARGUMENT，未初始化返回NOT_INITIALIZED。
     */
    DevDisplayStatus (*showDigit)(struct DevDisplay *self, DevDisplayPosition position,
                                 uint8_t digit, DevDisplayDecimalPoint decimal_point); // 单位数字的方法。
    /**
     * @brief 显示四位无小数点整数，高位补零。
     * @param self GetDisplay返回的设备对象。
     * @param value 整数，0至9999，超范围提交E04。
     * @return 成功返回OK，对象无效返回INVALID_ARGUMENT，未初始化返回NOT_INITIALIZED，超范围返回OUT_OF_RANGE。
     */
    DevDisplayStatus (*showInteger4)(struct DevDisplay *self, uint16_t value); // 整数显示的方法。
    /**
     * @brief 按前导零模式显示定点数，保留全部指定小数位并原子提交整帧。
     * @param self GetDisplay返回的设备对象。
     * @param scaled_value 按小数位放大的整数，0至9999；1234配合1位小数显示123.4。
     * @param decimal_places 小数位数，0至3；数值或小数位超范围提交E04。
     * @param mode 前导零模式，SUPPRESS抑制整数前导零，PAD补齐；非法值保持原显示。
     * @return 成功返回OK，对象或模式无效返回INVALID_ARGUMENT，未初始化返回NOT_INITIALIZED，超范围返回OUT_OF_RANGE。
     */
    DevDisplayStatus (*showFixedPoint)(struct DevDisplay *self, uint16_t scaled_value,
                                      uint8_t decimal_places,
                                      DevDisplayLeadingZeroMode mode); // 统一定点数显示的方法。
    /**
     * @brief 显示左侧空白、E和两位十进制错误码。
     * @param self GetDisplay返回的设备对象。
     * @param code 错误码，0至255，显示code模100的两位数字。
     * @return 成功返回OK，对象无效返回INVALID_ARGUMENT，未初始化返回NOT_INITIALIZED。
     */
    DevDisplayStatus (*showError)(struct DevDisplay *self, uint8_t code); // 错误码显示的方法。
    /**
     * @brief 提交四位所有段及小数点全亮帧，仍按单个位动态扫描。
     * @param self GetDisplay返回的设备对象。
     * @return 成功返回OK，对象无效返回INVALID_ARGUMENT，未初始化返回NOT_INITIALIZED。
     */
    DevDisplayStatus (*showAll)(struct DevDisplay *self); // 全段显示的方法。
    /**
     * @brief 立即关闭段选及所有位选，后续扫描保持关闭；内容更新可重新启用扫描。
     * @param self GetDisplay返回的设备对象。
     * @return 成功返回OK，对象无效返回INVALID_ARGUMENT，未初始化返回NOT_INITIALIZED。
     */
    DevDisplayStatus (*stop)(struct DevDisplay *self); // 停止物理输出的方法。
} DevDisplay;

/**
 * @brief 获取并绑定静态设备对象，不初始化GPIO，也不创建周期调度。
 * @return 始终返回非空静态对象，驱动可用性由init返回值判断。
 */
DevDisplay *GetDisplay(void);

#endif
