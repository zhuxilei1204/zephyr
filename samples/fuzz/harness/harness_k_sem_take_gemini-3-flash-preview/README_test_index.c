/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2026-01-05 19:12:57
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_k_sem_and_thread_custom_data_fuzz.c
//    测试名称: k_sem_and_thread_custom_data_fuzz
//    API类别: Kernel Services
//    描述: Fuzzes semaphore operations and thread custom data accessors. This test case exercises k_sem_take with non-blocking timeouts and validates thread-local custom data storage integrity using static memory references.


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
