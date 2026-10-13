#ifndef BSP_EXAMPLE_H
#define BSP_EXAMPLE_H

#define BSP_EXAMPLE_NOT_IMPLEMENTED (-2)

/**
 * @brief 未实现的板级示例初始化，不配置或访问任何硬件。
 * @return 固定返回 BSP_EXAMPLE_NOT_IMPLEMENTED，不能作为已完成驱动使用。
 */
int BSP_Example_Init(void);

#endif
