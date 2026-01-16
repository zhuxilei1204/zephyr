/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2025-12-30 12:08:08
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_zephyr_memory_management_fuzz.c
//    测试名称: zephyr_memory_management_fuzz
//    API类别: kernel_memory
//    描述: Fuzzing test for Zephyr memory management APIs including k_mem_free_get, k_mem_slab_num_free_get, and K_APP_BMEM macro usage. Exercises slab allocation/deallocation and system memory status reporting.


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
