# 共用支持代码

## 内容

- `Device/inc/dev_common.h`：模块共用的 `DevStatus` 状态类型，供 GPIO 与甲烷传感器接口使用。
- `Core/Inc/bsp_byte_ring.h`、`Core/Src/bsp_byte_ring.c`：固定容量字节环形缓冲区，供 USART3 接收和 RS485 接收使用。

## 依赖与移植

仅依赖标准 `stdint.h`。缓冲区大小在头文件中静态配置；迁移时确认容量、并发访问方式和中断/主循环生产消费关系。该缓冲区自身没有临界区或锁，调用者负责保证并发安全。

## 验证状态

从来源固件快照原样复制，本归档没有单独编译或验证。


## 2026-10-05 GPIO/执行器状态码兼容维护

共用DevStatus用于GPIO、执行器及原甲烷传感器接口。保留原DEV_OK=0、DEV_PENDING=1、DEV_TIMEOUT=2、DEV_FORMAT_ERROR=3、DEV_LRC_ERROR=4、DEV_SENSOR_ERROR=5、DEV_IO_ERROR=6；增加DEV_ARGUMENT_ERROR=-1、DEV_NOT_READY=-2，原驱动结果数值不变。工程按同一完整定义维护，工程旧IO=-3不再使用；判断应使用枚举符号，不使用正负号概括所有错误。

仅静态核对及当前G071正式工程构建通过；其他库驱动未重建、软件测试未运行、硬件未验证。日志在本分类驱动库更新日志.txt，旧驱动源码未修改。
