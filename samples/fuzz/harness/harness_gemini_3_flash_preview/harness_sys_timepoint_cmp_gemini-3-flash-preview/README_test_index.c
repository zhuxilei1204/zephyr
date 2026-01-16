/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2026-01-03 05:25:50
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_timepoint_and_thread_data_fuzz.c
//    测试名称: timepoint_and_thread_data_fuzz
//    API类别: Kernel Services
//    描述: Fuzzes timepoint calculation, comparison, and conversion, along with thread custom data access. Validates that timepoints calculated from timeouts can be compared and converted back to relative timeouts without crashing, and ensures thread custom data consistency.


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
