/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2026-01-04 19:59:00
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_kernel_obj_core_stats_management.c
//    测试名称: kernel_obj_core_stats_management
//    API类别: Kernel Objects
//    描述: Fuzzes the registration, enablement, and deregistration of kernel object statistics using a semaphore's object core. This test exercises the lifecycle of object statistics tracking including buffer registration with varying sizes.


/*
 * 编译说明:
 * 1. 每个测试文件都是独立的，可以单独编译
 * 2. 需要链接对应的RTOS库和项目代码
 * 3. 使用LibAFL或其他模糊测试工具进行测试
 * 
 * 使用方法:
 * 1. 选择要测试的API类别
 * 2. 编译对应的测试文件
 * 3. 在模糊测试环境中运行
 */
