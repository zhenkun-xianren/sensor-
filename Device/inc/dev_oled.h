#ifndef DEV_OLED_H
#define DEV_OLED_H

#include <stdint.h>

#define DEV_OLED_LINE_COUNT 8U
#define DEV_OLED_TEXT_MAX 21U

/** @brief 与芯片和板级引脚无关的 OLED 显示设备接口。 */
typedef struct DevOled DevOled;
struct DevOled {
    /**
     * @brief 初始化显示模块并清屏。
     * @param self 接收初始化请求的 OLED 设备对象。
     * @return 成功返回 1，接口或硬件初始化失败返回 0。
     */
    uint8_t (*init)(DevOled *self);

    /**
     * @brief 在首行显示 HelloWorld 与一个十进制计数值。
     * @param self 接收显示请求的 OLED 设备对象。
     * @param value 从零开始递增的无符号计数值。
     * @return 成功返回 1，设备未就绪或通信失败返回 0。
     */
    uint8_t (*showCounter)(DevOled *self, uint32_t value);
    /**
     * @brief 写入整行 ASCII 文本并清除上一内容残留。
     * @param self 接收请求的 OLED 设备对象。
     * @param line 目标行号，范围0至7，每行8像素高。
     * @param text 以零结束的文本，最多21个可打印ASCII字符；未提供字形的字符显示问号。
     * @return 成功返回1，参数无效、设备未就绪或通信失败返回0。
     */
    uint8_t (*writeLine)(DevOled *self, uint8_t line, const char *text);
};

/**
 * @brief 获取 OLED 设备接口对象。
 * @return 指向静态 OLED 设备对象的指针。
 */
DevOled *GetOled(void);

#endif /* DEV_OLED_H */
