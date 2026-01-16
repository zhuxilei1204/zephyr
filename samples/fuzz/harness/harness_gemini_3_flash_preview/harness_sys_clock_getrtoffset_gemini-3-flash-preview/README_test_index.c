/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2025-12-31 04:17:56
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_test_sys_clock_getrtoffset_integrity.c
//    测试名称: test_sys_clock_getrtoffset_integrity
//    API类别: Clock
//    描述: Fuzzes the sys_clock_getrtoffset kernel API to ensure it correctly populates a timespec structure and maintains consistency across busy-waits and system uptime checks. Validates that the nanosecond field remains within the legal POSIX range [0, 1,000,000,000).


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
