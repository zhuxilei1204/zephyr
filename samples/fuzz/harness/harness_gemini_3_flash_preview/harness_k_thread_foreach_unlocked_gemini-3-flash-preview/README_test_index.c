/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2025-12-31 09:54:15
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_kernel_thread_iteration_fuzz.c
//    测试名称: kernel_thread_iteration_fuzz
//    API类别: kernel
//    描述: Fuzzes thread iteration and synchronization APIs including k_thread_foreach, k_thread_foreach_unlocked, k_thread_foreach_unlocked_filter_by_cpu, k_spin_lock, and k_timer status.


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
