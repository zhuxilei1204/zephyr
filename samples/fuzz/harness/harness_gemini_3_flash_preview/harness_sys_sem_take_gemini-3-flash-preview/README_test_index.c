/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2025-12-31 18:31:33
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_fuzz_sem_take_logic.c
//    测试名称: fuzz_sem_take_logic
//    API类别: Kernel Semaphore
//    描述: Fuzzes k_sem_take and sys_sem_take by performing a sequence of give/take operations with varying initial states and limits. Ensures no blocking occurs by using K_NO_WAIT.


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
