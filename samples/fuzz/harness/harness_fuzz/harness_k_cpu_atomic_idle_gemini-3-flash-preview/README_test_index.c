/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2025-12-30 06:59:38
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_test_kernel_cpu_idle_control.c
//    测试名称: test_kernel_cpu_idle_control
//    API类别: kernel
//    描述: Fuzzes k_cpu_idle and k_cpu_atomic_idle. These APIs are responsible for putting the CPU into a low-power state until an interrupt occurs. The test toggles between standard and atomic idle states using fuzzed input.


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
