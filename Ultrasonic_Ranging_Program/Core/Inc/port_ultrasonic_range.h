#ifndef PORT_ULTRASONIC_RANGE_H
#define PORT_ULTRASONIC_RANGE_H

#include "dev_ultrasonic_range.h"

/* 单程距离换算为往返时间的一半；使用64位中间值避免乘法溢出。 */
#define PORT_ULTRASONIC_RANGE_ROUND_TRIP_SCALE 2000000ULL

#endif
