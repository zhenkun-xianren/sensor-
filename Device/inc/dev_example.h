#ifndef DEV_EXAMPLE_H
#define DEV_EXAMPLE_H

/** @brief 框架示例的设备操作结果；不对应任何实际产品协议。 */
typedef enum {
    DEV_EXAMPLE_OK = 0, /**< 操作成功。 */
    DEV_EXAMPLE_INVALID_ARGUMENT = -1, /**< 设备对象为空或不是本接口对象。 */
    DEV_EXAMPLE_NOT_IMPLEMENTED = -2 /**< BSP 尚未实现，不能作为可用驱动。 */
} DevExampleStatus;

/** @brief 用于展示四层调用关系的设备接口对象。 */
typedef struct DevExample DevExample;

/** @brief 示例设备的能力契约，不携带芯片、引脚或 RTOS 信息。 */
struct DevExample {
    /**
     * @brief 初始化示例设备；当前 BSP 尚未实现。
     * @param self 接收请求的设备对象，必须是 GetExample 返回的非空对象。
     * @return 对象无效返回 DEV_EXAMPLE_INVALID_ARGUMENT；当前有效调用返回
     * DEV_EXAMPLE_NOT_IMPLEMENTED；未来实现成功才可返回 DEV_EXAMPLE_OK。
     */
    DevExampleStatus (*init)(DevExample *self); // 初始化方法，通过同一设备对象调用。
};

/**
 * @brief 获取示例设备接口，获取对象不等于驱动已实现。
 * @return 文件内静态设备对象的非空指针，绑定初始化方法；不操作硬件。
 */
DevExample *GetExample(void);

#endif
