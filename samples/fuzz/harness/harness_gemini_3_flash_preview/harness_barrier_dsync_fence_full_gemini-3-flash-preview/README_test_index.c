/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2026-01-05 14:59:12
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_barrier_and_dt_iteration_fuzz.c
//    测试名称: barrier_and_dt_iteration_fuzz
//    API类别: CPU_BARRIER
//    描述: Fuzzes memory barrier synchronization and devicetree iteration macros. It uses the Data Synchronization Barrier (DSB) and iterates through the devicetree root children to ensure consistency between memory operations and hardware state representation, driven by fuzz input.


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
