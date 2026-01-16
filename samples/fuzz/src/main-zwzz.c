/*
 * Zephyr k_heap 强化版模糊测试 Harness
 * 优化重点：内存染色、对齐验证、完整性校验、多 API 覆盖
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/math_extras.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <stdint.h>

// --- LibAFL 配置 ---
#define MAX_FUZZ_INPUT_SIZE 1024
__attribute__((used, visibility("default"))) 
__attribute__((used)) unsigned char FUZZ_INPUT[MAX_FUZZ_INPUT_SIZE] = {
    // Offset 00000000
    0x90, 0xa0, 0xb0, 0x7f, 0x5f, 0x70, 0x7f, 0x8f, 
    0xe1, 0xbe, 0x46, 0x55, 0x5a, 0x5a, 0x01, 0x23,
    // Offset 00000010
    0x45, 0x70, 0x80, 0x90, 0xdf, 0x30, 0x40, 0x50, 
    0x60, 0x70, 0x80, 0x40, 0xe7
};// 初始种子

/*
 * Ensure FUZZ_INPUT is retained in the final image even when building
 * in non-fuzz modes (e.g., HEAP_SELFTEST_ONLY) where it might otherwise
 * be removed by aggressive GC/LTO.
 */
static volatile uintptr_t fuzz_input_anchor;

// --- 测试配置 ---
#define MAX_ALLOCS 12      // 增加指针池大小以提升碎片化复杂度
#define HEAP_SIZE 4096     // 扩大堆空间
#define MIN_ALIGN 4
#define MAX_ALIGN 64

static uint8_t heap_mem[HEAP_SIZE] __aligned(64);
static struct k_heap my_heap;

// 记录分配信息的结构体，用于检测 OOB (越界)
struct alloc_info {
    void *ptr;
    size_t size;
    uint8_t magic; // 用于校验内存内容的幻数
};

static struct alloc_info slots[MAX_ALLOCS];

static inline void validate_heap_or_trap(void)
{
    if (!sys_heap_validate(&my_heap.heap)) {
        printk("FATAL: sys_heap_validate failed\n");
        __builtin_trap();
    }
}

// --- 基础辅助工具 (FR_Reader) ---
typedef struct {
    const unsigned char* data;
    size_t size;
    size_t off;
} FR_Reader;

static inline FR_Reader FR_init(const unsigned char* buf, size_t n) {
    return (FR_Reader){ buf, n, 0 };
}

static inline uint8_t FR_next_u8(FR_Reader* r) {
    return (r->off < r->size) ? r->data[r->off++] : 0;
}

static inline uint32_t FR_next_u32(FR_Reader* r) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= ((uint32_t)FR_next_u8(r) << (i * 8));
    return v;
}

static inline uint32_t FR_next_range(FR_Reader* r, uint32_t min, uint32_t max) {
    if (max <= min) return min;
    return min + (FR_next_u32(r) % (max - min + 1));
}

// --- 强化功能函数 ---

// 1. 内存填充：向分配的块写入特定模式，用于检测非法篡改
static void scribble_memory(struct alloc_info *info) {
    if (info->ptr && info->size > 0) {
        memset(info->ptr, info->magic, info->size);
    }
}

// 2. 内存校验：释放前检查内容是否被意外修改
static void verify_memory(struct alloc_info *info) {
    if (!info->ptr) return;
    uint8_t *p = (uint8_t *)info->ptr;
    for (size_t i = 0; i < info->size; i++) {
        if (p[i] != info->magic) {
            printk("FATAL: Memory corruption detected at %p (offset %zu)\n", p, i);
            __builtin_trap(); // 强制触发 Fuzzer 崩溃捕获
        }
    }
}

// 3. 停机指令
int __attribute__((noinline)) BREAKPOINT(void) {
    for (;;) { __asm__ volatile("nop"); }
}

// --- 核心测试函数 ---
void test_once(const uint8_t *input, size_t len) {
    FR_Reader r = FR_init(input, len);
    
    // 重置状态
    k_heap_init(&my_heap, heap_mem, HEAP_SIZE);
    memset(slots, 0, sizeof(slots));
    validate_heap_or_trap();

    // 从输入决定执行多少步操作 (10-50步)
    uint32_t steps = FR_next_range(&r, 10, 50);

    for (uint32_t i = 0; i < steps; i++) {
        uint8_t action = FR_next_u8(&r) % 6; // 六种动作
        uint8_t slot_idx = FR_next_u8(&r) % MAX_ALLOCS;
        struct alloc_info *slot = &slots[slot_idx];

        switch (action) {
            case 0: // 常规分配 (k_heap_alloc)
                if (!slot->ptr) {
                    slot->size = (size_t)FR_next_range(&r, 1, 512);
                    slot->magic = FR_next_u8(&r);
                    slot->ptr = k_heap_alloc(&my_heap, slot->size, K_NO_WAIT);
                    scribble_memory(slot);
                    validate_heap_or_trap();
                }
                break;

            case 1: // 对齐分配 (k_heap_aligned_alloc)
                if (!slot->ptr) {
                    // 随机对齐量：4, 8, 16, 32, 64
                    size_t align = 1 << FR_next_range(&r, 2, 6); 
                    slot->size = (size_t)FR_next_range(&r, 1, 512);
                    slot->magic = FR_next_u8(&r);
                    slot->ptr = k_heap_aligned_alloc(&my_heap, align, slot->size, K_NO_WAIT);
                    
                    // 验证对齐是否生效
                    if (slot->ptr && ((uintptr_t)slot->ptr % align != 0)) {
                        printk("FATAL: Alignment error! ptr=%p, align=%zu\n", slot->ptr, align);
                        __builtin_trap();
                    }
                    scribble_memory(slot);
                    validate_heap_or_trap();
                }
                break;

            case 2: // 释放内存 (k_heap_free)
                if (slot->ptr) {
                    verify_memory(slot); // 释放前校验，捕获静默越界
                    k_heap_free(&my_heap, slot->ptr);
                    slot->ptr = NULL;
                    validate_heap_or_trap();
                }
                break;

            case 3: // 模拟“使用中”读取
                if (slot->ptr) {
                    verify_memory(slot);
                }
                break;

            case 4: // 重新分配 (k_heap_realloc)
                if (slot->ptr) {
                    verify_memory(slot);
                    size_t new_size = (size_t)FR_next_range(&r, 0, 768);
                    void *p2 = k_heap_realloc(&my_heap, slot->ptr, new_size, K_NO_WAIT);
                    if (new_size == 0) {
                        /* realloc(ptr,0) frees and returns NULL */
                        if (p2 != NULL) {
                            printk("FATAL: realloc(ptr,0) returned non-NULL\n");
                            __builtin_trap();
                        }
                        slot->ptr = NULL;
                        slot->size = 0;
                    } else if (p2 != NULL) {
                        slot->ptr = p2;
                        slot->size = new_size;
                        scribble_memory(slot);
                    } else {
                        /* realloc failed: original pointer remains valid */
                        verify_memory(slot);
                    }
                    validate_heap_or_trap();
                }
                break;

            case 5: // calloc (k_heap_calloc)
                if (!slot->ptr) {
                    size_t n = (size_t)FR_next_range(&r, 1, 64);
                    size_t sz = (size_t)FR_next_range(&r, 1, 64);
                    size_t total;
                    if (size_mul_overflow(n, sz, &total) || total == 0 || total > 512) {
                        break;
                    }

                    void *p = k_heap_calloc(&my_heap, n, sz, K_NO_WAIT);
                    if (p != NULL) {
                        /* Verify calloc semantics for requested bytes only */
                        uint8_t *b = (uint8_t *)p;
                        for (size_t j = 0; j < total; j++) {
                            if (b[j] != 0) {
                                printk("FATAL: calloc not zeroed (offset %zu)\n", j);
                                __builtin_trap();
                            }
                        }

                        slot->ptr = p;
                        slot->size = total;
                        slot->magic = FR_next_u8(&r);
                        scribble_memory(slot);
                        validate_heap_or_trap();
                    }
                }
                break;
        }
    }

    // 清理：释放所有遗留内存，防止干扰下一轮测试
    for (int i = 0; i < MAX_ALLOCS; i++) {
        if (slots[i].ptr) {
            verify_memory(&slots[i]);
            k_heap_free(&my_heap, slots[i].ptr);
        }
    }

    validate_heap_or_trap();
    
    printk("[TEST_CASE_COMPLETED]\n");
}

int main(void) {
    /* Keep a runtime reference to FUZZ_INPUT for external runners. */
    fuzz_input_anchor = (uintptr_t)FUZZ_INPUT;
#ifdef HEAP_SELFTEST_ONLY
    extern void heap_selftest_run(void);
    heap_selftest_run();
    printk("[SELFTEST_DONE]\n");
    k_msleep(1);
    BREAKPOINT();
    return 0;
#else
    test_once(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
    k_msleep(1);
    BREAKPOINT();
    return 0;
#endif
}