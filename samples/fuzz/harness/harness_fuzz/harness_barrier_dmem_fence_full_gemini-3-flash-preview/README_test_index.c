/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2026-01-02 16:26:44
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_barrier_dmem_fence_full_ring_buffer_fuzz.c
//    测试名称: barrier_dmem_fence_full_ring_buffer_fuzz
//    API类别: CPU_BARRIERS
//    描述: Fuzzes the barrier_dmem_fence_full API by simulating a producer-consumer pattern using a ring buffer. The test ensures that the memory barrier can be called repeatedly in conjunction with data operations without causing instability or memory corruption in a single-threaded environment.


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
