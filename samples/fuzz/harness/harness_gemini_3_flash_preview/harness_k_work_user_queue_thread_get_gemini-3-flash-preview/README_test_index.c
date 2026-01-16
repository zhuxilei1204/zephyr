/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2026-01-03 14:54:31
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_work_queue_thread_management_fuzz.c
//    测试名称: work_queue_thread_management_fuzz
//    API类别: Kernel Services
//    描述: Fuzzes the initialization and thread retrieval of user-mode and kernel-mode work queues. It tests k_work_user_queue_start, k_work_user_queue_thread_get, and k_work_queue_thread_get by varying priorities and thread names.


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
