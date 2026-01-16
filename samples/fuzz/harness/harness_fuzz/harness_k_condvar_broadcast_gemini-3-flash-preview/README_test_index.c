/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2025-12-30 07:01:19
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_k_condvar_broadcast_fuzz.c
//    测试名称: k_condvar_broadcast_fuzz
//    API类别: Kernel/Synchronization
//    描述: Fuzzes k_condvar_init and k_condvar_broadcast to verify correct behavior in a single-threaded environment where no threads are waiting. It tests initialization, signaling, and broadcasting on a pool of condition variables.


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
