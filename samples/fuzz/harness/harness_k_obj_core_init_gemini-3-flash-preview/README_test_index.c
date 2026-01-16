/*
 * RTOS模糊测试 - 测试用例索引
 * 生成时间: 2026-01-06 15:23:41
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * 
 * 本目录包含 1 个独立的测试用例文件
 */

// 测试用例列表:

// 1. test_00_k_obj_core_lifecycle_fuzz.c
//    测试名称: k_obj_core_lifecycle_fuzz
//    API类别: kernel
//    描述: Fuzzes the kernel object core initialization and linking lifecycle, including k_obj_core_init and k_obj_core_init_and_link. It also exercises related Devicetree and syscall macros in a safe context.


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
