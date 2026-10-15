#ifndef PORT_ULTRASONIC_RANGE_H
#define PORT_ULTRASONIC_RANGE_H

#include "dev_ultrasonic_range.h"

/* 根据公共传播程数换算路径长度；使用64位中间值避免乘法溢出。 */
#define PORT_ULTRASONIC_RANGE_TIME_SCALE (DEV_ULTRASONIC_RANGE_PATH_LEGS * 1000000ULL)
#if (DEV_ULTRASONIC_RANGE_PATH_LEGS != 1ULL) && (DEV_ULTRASONIC_RANGE_PATH_LEGS != 2ULL)
#error "Ultrasonic path legs must be one-way or round-trip"
#endif

#endif
