/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2025-12-31 00:00:52
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_k_queue_and_thread_name_fuzz.c
//    测试名称: k_queue_and_thread_name_fuzz
//    API类别: kernel
//    描述: Fuzzes k_queue operations (init, append, is_empty) and k_thread_name_set. Uses a static pool of items to safely manage queue memory and ensures non-blocking execution to prevent deadlocks in a single-threaded environment.


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
