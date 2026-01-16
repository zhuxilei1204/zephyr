// /*
//  * Zephyr k_heap 强化版模糊测试 Harness (物理隔离 + 单次执行版)
//  */

// #include <zephyr/kernel.h>
// #include <zephyr/sys/printk.h>
// #include <zephyr/sys/math_extras.h>
// #include <zephyr/sys/sys_heap.h>
// #include <zephyr/sys/util.h>
// #include <string.h>
// #include <stdint.h>

// #define MAX_FUZZ_INPUT_SIZE 1024
// #define GUARD_SIZE 1024  // 1KB 隔离带

// // --- 1. 数据段与全局输入隔离 ---
// __attribute__((used, visibility("default"))) 
// unsigned char FUZZ_INPUT[MAX_FUZZ_INPUT_SIZE] = {
//     0x90, 0xa0, 0xb0, 0x7f, 0x5f, 0x70, 0x7f, 0x8f, 
//     0xe1, 0xbe, 0x46, 0x55, 0x5a, 0x5a, 0x01, 0x23,
//     0x45, 0x70, 0x80, 0x90, 0xdf, 0x30, 0x40, 0x50, 
//     0x60, 0x70, 0x80, 0x40, 0xe7
// };

// // --- 2. 堆内存物理隔离带（判定性实验：guard + 校验） ---
// #define HEAP_SIZE 4096
// struct guarded_heap_mem {
//     uint8_t pre[GUARD_SIZE];
//     uint8_t mem[HEAP_SIZE] __aligned(64);
//     uint8_t post[GUARD_SIZE];
// };
// static struct guarded_heap_mem g_heap_mem;

// // --- 3. 指针池及其结构隔离 ---
// struct alloc_info {
//     void *ptr;
//     size_t size;
//     uint8_t magic;
// };
// #define MAX_ALLOCS 12
// struct guarded_slots {
//     uint8_t pre[GUARD_SIZE];
//     struct alloc_info slots[MAX_ALLOCS];
//     uint8_t post[GUARD_SIZE];
// };
// static struct guarded_slots g_slots;

// // --- 4. 内核堆对象隔离 ---
// static uint8_t _guard_heap_obj_pre[GUARD_SIZE];
// static struct k_heap my_heap;
// static uint8_t _guard_heap_obj_post[GUARD_SIZE];

// // --- 5. 遥测数据隔离 ---
// struct fuzz_last_op {
//     uint32_t step;
//     uint32_t action;
//     uint32_t slot;
//     uint32_t size;
//     uint32_t aux;
//     uint32_t ptr;
// };
// static uint8_t _guard_tele_pre[GUARD_SIZE];
// __attribute__((used, visibility("default")))
// volatile struct fuzz_last_op FUZZ_LAST_OP;

// __attribute__((used, visibility("default")))
// volatile uint32_t FUZZ_PRINT_LAST_OP;

// // --- 辅助工具函数 ---

// int __attribute__((noinline, used, visibility("default"))) BREAKPOINT(void)
// {
//     __asm__ volatile("" ::: "memory");
//     return 0;
// }

// static inline void guard_fill(uint8_t *buf, size_t n, uint8_t pattern) {
//     memset(buf, pattern, n);
// }

// static inline void guard_check_or_trap(const char *name, const uint8_t *buf, size_t n, uint8_t pattern) {
//     for (size_t i = 0; i < n; i++) {
//         if (buf[i] != pattern) {
//             printk("[GUARD_CORRUPTION] %s off=%zu exp=0x%02x got=0x%02x\n",
//                    name, i, pattern, buf[i]);
//             printk("[LAST_OP] step=%u action=%u slot=%u size=%u aux=%u ptr=0x%08x\n",
//                    FUZZ_LAST_OP.step,
//                    FUZZ_LAST_OP.action,
//                    FUZZ_LAST_OP.slot,
//                    FUZZ_LAST_OP.size,
//                    FUZZ_LAST_OP.aux,
//                    FUZZ_LAST_OP.ptr);
//             __builtin_trap();
//         }
//     }
// }

// static inline void guards_init(void) {
//     guard_fill(g_heap_mem.pre, GUARD_SIZE, 0xA5);
//     guard_fill(g_heap_mem.post, GUARD_SIZE, 0x5A);
//     guard_fill(g_slots.pre, GUARD_SIZE, 0xC3);
//     guard_fill(g_slots.post, GUARD_SIZE, 0x3C);
// }

// static inline void guards_check_or_trap(void) {
//     guard_check_or_trap("heap_pre", g_heap_mem.pre, GUARD_SIZE, 0xA5);
//     guard_check_or_trap("heap_post", g_heap_mem.post, GUARD_SIZE, 0x5A);
//     guard_check_or_trap("slots_pre", g_slots.pre, GUARD_SIZE, 0xC3);
//     guard_check_or_trap("slots_post", g_slots.post, GUARD_SIZE, 0x3C);
// }

// static inline void validate_heap_or_trap(void) {
//     if (!sys_heap_validate(&my_heap.heap)) {
//         printk("CRITICAL: Heap structure corrupted!\n");
//         __builtin_trap();
//     }
// }

// typedef struct {
//     const unsigned char* data;
//     size_t size;
//     size_t off;
// } FR_Reader;

// static inline FR_Reader FR_init(const unsigned char* buf, size_t n) {
//     return (FR_Reader){ buf, n, 0 };
// }

// static inline uint8_t FR_next_u8(FR_Reader* r) {
//     return (r->off < r->size) ? r->data[r->off++] : 0;
// }

// static inline uint32_t FR_next_u32(FR_Reader* r) {
//     uint32_t v = 0;
//     for (int i = 0; i < 4; i++) v |= ((uint32_t)FR_next_u8(r) << (i * 8));
//     return v;
// }

// static inline uint32_t FR_next_range(FR_Reader* r, uint32_t min, uint32_t max) {
//     if (max <= min) return min;
//     return min + (FR_next_u32(r) % (max - min + 1));
// }

// static void scribble_memory(struct alloc_info *info) {
//     if (info->ptr && info->size > 0) {
//         memset(info->ptr, info->magic, info->size);
//     }
// }

// static void verify_memory(struct alloc_info *info) {
//     if (!info->ptr || info->size == 0) return;
//     uint8_t *p = (uint8_t *)info->ptr;
//     for (size_t i = 0; i < info->size; i++) {
//         if (p[i] != info->magic) {
//             printk("OOB DETECTED: Slot corrupted at %p offset %zu\n", p, i);
//             __builtin_trap();
//         }
//     }
// }

// // --- 核心测试逻辑 ---

// void test_once(const uint8_t *input, size_t len) {
//     FR_Reader r = FR_init(input, len);

//     guards_init();
//     k_heap_init(&my_heap, g_heap_mem.mem, HEAP_SIZE);
//     memset(&g_slots.slots[0], 0, sizeof(g_slots.slots));
//     guards_check_or_trap();
    
//     uint32_t steps = FR_next_range(&r, 10, 50);

//     for (uint32_t i = 0; i < steps; i++) {
//         uint8_t action = FR_next_u8(&r) % 6; 
//         uint8_t slot_idx = FR_next_u8(&r) % MAX_ALLOCS;
//         struct alloc_info *slot = &g_slots.slots[slot_idx];

//         FUZZ_LAST_OP.step = i;
//         FUZZ_LAST_OP.action = action;
//         FUZZ_LAST_OP.slot = slot_idx;
//         FUZZ_LAST_OP.size = 0;
//         FUZZ_LAST_OP.aux = 0;
//         FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;

//         switch (action) {
//             case 0: // k_heap_alloc
//                 if (!slot->ptr) {
//                     slot->size = (size_t)FR_next_range(&r, 1, 512);
//                     slot->magic = FR_next_u8(&r);
//                     FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                     slot->ptr = k_heap_alloc(&my_heap, slot->size, K_NO_WAIT);
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                     if (slot->ptr) scribble_memory(slot);
//                     validate_heap_or_trap();
//                     guards_check_or_trap();
//                 }
//                 break;

//             case 1: // k_heap_aligned_alloc
//                 if (!slot->ptr) {
//                     size_t align = 1 << FR_next_range(&r, 2, 6); 
//                     slot->size = (size_t)FR_next_range(&r, 1, 512);
//                     slot->magic = FR_next_u8(&r);
//                     FUZZ_LAST_OP.aux = (uint32_t)align;
//                     FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                     slot->ptr = k_heap_aligned_alloc(&my_heap, align, slot->size, K_NO_WAIT);
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                     if (slot->ptr) {
//                         if (((uintptr_t)slot->ptr % align) != 0) __builtin_trap();
//                         scribble_memory(slot);
//                     }
//                     validate_heap_or_trap();
//                     guards_check_or_trap();
//                 }
//                 break;

//             case 2: // k_heap_free
//                 if (slot->ptr) {
//                     FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                     verify_memory(slot);
//                     k_heap_free(&my_heap, slot->ptr);
//                     slot->ptr = NULL;
//                     FUZZ_LAST_OP.ptr = 0;
//                     validate_heap_or_trap();
//                     guards_check_or_trap();
//                 }
//                 break;

//             case 3: // verify
//                 if (slot->ptr) {
//                     FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                     verify_memory(slot);
//                 }
//                 break;

//             case 4: // k_heap_realloc
//                 if (slot->ptr) {
//                     FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                     verify_memory(slot);
//                     size_t new_size = (size_t)FR_next_range(&r, 0, 768);
//                     FUZZ_LAST_OP.size = (uint32_t)new_size;
//                     void *p2 = k_heap_realloc(&my_heap, slot->ptr, new_size, K_NO_WAIT);
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)p2;
//                     if (new_size == 0) {
//                         slot->ptr = NULL;
//                         slot->size = 0;
//                     } else if (p2 != NULL) {
//                         slot->ptr = p2;
//                         slot->size = new_size;
//                         scribble_memory(slot);
//                     }
//                     validate_heap_or_trap();
//                     guards_check_or_trap();
//                 }
//                 break;

//             case 5: // k_heap_calloc
//                 if (!slot->ptr) {
//                     size_t nmemb = (size_t)FR_next_range(&r, 1, 32);
//                     size_t size = (size_t)FR_next_range(&r, 1, 32);
//                     size_t total;
//                     if (size_mul_overflow(nmemb, size, &total) || total > 512) break;

//                     FUZZ_LAST_OP.aux = (uint32_t)nmemb;
//                     FUZZ_LAST_OP.size = (uint32_t)total;

//                     void *p = k_heap_calloc(&my_heap, nmemb, size, K_NO_WAIT);
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)p;
//                     if (p) {
//                         uint8_t *check = (uint8_t *)p;
//                         for(size_t j=0; j<total; j++) if(check[j] != 0) __builtin_trap();
//                         slot->ptr = p;
//                         slot->size = total;
//                         slot->magic = FR_next_u8(&r);
//                         scribble_memory(slot);
//                     }
//                     validate_heap_or_trap();
//                     guards_check_or_trap();
//                 }
//                 break;
//         }
//     }

//     // 清理资源
//     for (int i = 0; i < MAX_ALLOCS; i++) {
//         if (g_slots.slots[i].ptr) {
//             k_heap_free(&my_heap, g_slots.slots[i].ptr);
//             g_slots.slots[i].ptr = NULL;
//         }
//     }

//     validate_heap_or_trap();
//     guards_check_or_trap();

//     if (FUZZ_PRINT_LAST_OP) {
//         printk("[LAST_OP] step=%u action=%u slot=%u ptr=0x%08x\n",
//                FUZZ_LAST_OP.step, FUZZ_LAST_OP.action, FUZZ_LAST_OP.slot, (uint32_t)(uintptr_t)FUZZ_LAST_OP.ptr);
//     }
// }

// int main(void) {
//     // 单次运行，由 Fuzzer 控制外部重启/快照
//     test_once(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

//     printk("[TEST_CASE_COMPLETED]\n");

//     // 关键：强制睡眠触发上下文切换，检测 idle 线程 TCB 是否存活
//     k_msleep(1); 

//     printk("Fuzzing cycle end.\n");
    
//     // 触发断点通知 QEMU 停止执行
//     volatile int bp = BREAKPOINT();
//     (void)bp;

//     return 0;
// }

/*
 * Zephyr k_heap 终极防御版 Fuzz Harness
 * 目标：消除布局随机性，主动检测溢出，隔离栈干扰
 */

// #include <zephyr/kernel.h>
// #include <zephyr/sys/printk.h>
// #include <zephyr/sys/math_extras.h>
// #include <zephyr/sys/sys_heap.h>
// #include <zephyr/sys/util.h>
// #include <string.h>
// #include <stdint.h>
// /*
//  * Zephyr k_heap fuzz harness (runner-compatible)
//  *
//  * Goals:
//  * - Export stable symbols for the QEMU baremetal breakpoint runner:
//  *     - FUZZ_INPUT (1024 bytes)
//  *     - FUZZ_LAST_OP (telemetry)
//  *     - BREAKPOINT (end-of-iteration marker)
//  * - Avoid placing FUZZ_INPUT at 0x20000000 (often used as a "magic" MMIO address
//  *   in runners) by inserting a small padding object at the start of .bss.
//  * - Add guard-band checks + sys_heap_validate to gather hard evidence when
//  *   memory corruption happens.
//  */

// #define MAX_FUZZ_INPUT_SIZE 1024

// /*
//  * If a runner uses a magic write hook at 0x20000004, placing FUZZ_INPUT at
//  * 0x20000000 will cause immediate crashes when the runner writes inputs.
//  *
//  * Put a small padding object in a .bss.* section that sorts before FUZZ_INPUT's
//  * section, so FUZZ_INPUT lands at 0x20000200 (or later).
//  */
// __attribute__((used, section(".bss.00_fuzz_pad")))
// volatile uint8_t FUZZ_PAD[512];

// __attribute__((used, visibility("default"), section(".bss.10_fuzz_input")))
// unsigned char FUZZ_INPUT[MAX_FUZZ_INPUT_SIZE];

// struct fuzz_last_op {
//     uint32_t step;
//     uint32_t action;
//     uint32_t slot;
//     uint32_t size;
//     uint32_t aux;
//     uint32_t ptr;
// };

// __attribute__((used, visibility("default")))
// volatile struct fuzz_last_op FUZZ_LAST_OP;

// __attribute__((used, visibility("default")))
// volatile uint32_t FUZZ_PRINT_LAST_OP;

// int __attribute__((noinline, used, visibility("default"))) BREAKPOINT(void)
// {
//     __asm__ volatile("" ::: "memory");
//     return 0;
// }

// // --- Guard-band experiment ---
// #define GUARD_SIZE 1024
// #define HEAP_SIZE 4096

// struct guarded_heap_mem {
//     uint8_t pre[GUARD_SIZE];
//     uint8_t mem[HEAP_SIZE] __aligned(64);
//     uint8_t post[GUARD_SIZE];
// };

// struct alloc_info {
//     void *ptr;
//     size_t size;
//     uint8_t magic;
// };

// #define MAX_ALLOCS 12

// struct guarded_slots {
//     uint8_t pre[GUARD_SIZE];
//     struct alloc_info slots[MAX_ALLOCS];
//     uint8_t post[GUARD_SIZE];
// };

// static struct guarded_heap_mem g_heap_mem;
// static struct guarded_slots g_slots;
// static struct k_heap my_heap;

// static inline void guard_fill(uint8_t *buf, size_t n, uint8_t pattern)
// {
//     memset(buf, pattern, n);
// }

// static inline void guard_check_or_trap(const char *name, const uint8_t *buf, size_t n, uint8_t pattern)
// {
//     for (size_t i = 0; i < n; i++) {
//         if (buf[i] != pattern) {
//             printk("[GUARD_CORRUPTION] %s off=%zu exp=0x%02x got=0x%02x\n", name, i, pattern, buf[i]);
//             printk("[LAST_OP] step=%u action=%u slot=%u size=%u aux=%u ptr=0x%08x\n",
//                    FUZZ_LAST_OP.step, FUZZ_LAST_OP.action, FUZZ_LAST_OP.slot,
//                    FUZZ_LAST_OP.size, FUZZ_LAST_OP.aux, FUZZ_LAST_OP.ptr);
//             __builtin_trap();
//         }
//     }
// }

// static inline void guards_init(void)
// {
//     guard_fill(g_heap_mem.pre, GUARD_SIZE, 0xA5);
//     guard_fill(g_heap_mem.post, GUARD_SIZE, 0x5A);
//     guard_fill(g_slots.pre, GUARD_SIZE, 0xC3);
//     guard_fill(g_slots.post, GUARD_SIZE, 0x3C);
// }

// static inline void guards_check_or_trap(void)
// {
//     guard_check_or_trap("heap_pre", g_heap_mem.pre, GUARD_SIZE, 0xA5);
//     guard_check_or_trap("heap_post", g_heap_mem.post, GUARD_SIZE, 0x5A);
//     guard_check_or_trap("slots_pre", g_slots.pre, GUARD_SIZE, 0xC3);
//     guard_check_or_trap("slots_post", g_slots.post, GUARD_SIZE, 0x3C);
// }

// static inline void validate_heap_or_trap(void)
// {
//     if (!sys_heap_validate(&my_heap.heap)) {
//         printk("CRITICAL: Heap structure corrupted!\n");
//         printk("[LAST_OP] step=%u action=%u slot=%u size=%u aux=%u ptr=0x%08x\n",
//                FUZZ_LAST_OP.step, FUZZ_LAST_OP.action, FUZZ_LAST_OP.slot,
//                FUZZ_LAST_OP.size, FUZZ_LAST_OP.aux, FUZZ_LAST_OP.ptr);
//         __builtin_trap();
//     }
// }

// typedef struct {
//     const unsigned char *data;
//     size_t size;
//     size_t off;
// } FR_Reader;

// static inline FR_Reader FR_init(const unsigned char *buf, size_t n)
// {
//     return (FR_Reader){ buf, n, 0 };
// }

// static inline uint8_t FR_next_u8(FR_Reader *r)
// {
//     return (r->off < r->size) ? r->data[r->off++] : 0;
// }

// static inline uint32_t FR_next_u32(FR_Reader *r)
// {
//     uint32_t v = 0;
//     for (int i = 0; i < 4; i++) {
//         v |= ((uint32_t)FR_next_u8(r) << (i * 8));
//     }
//     return v;
// }

// static inline uint32_t FR_next_range(FR_Reader *r, uint32_t min, uint32_t max)
// {
//     if (max <= min) {
//         return min;
//     }
//     return min + (FR_next_u32(r) % (max - min + 1));
// }

// static void scribble_memory(struct alloc_info *info)
// {
//     if (info->ptr && info->size > 0) {
//         memset(info->ptr, info->magic, info->size);
//     }
// }

// static void verify_memory(struct alloc_info *info)
// {
//     if (!info->ptr || info->size == 0) {
//         return;
//     }
//     uint8_t *p = (uint8_t *)info->ptr;
//     for (size_t i = 0; i < info->size; i++) {
//         if (p[i] != info->magic) {
//             printk("OOB DETECTED: Slot corrupted at %p offset %zu\n", p, i);
//             printk("[LAST_OP] step=%u action=%u slot=%u size=%u aux=%u ptr=0x%08x\n",
//                    FUZZ_LAST_OP.step, FUZZ_LAST_OP.action, FUZZ_LAST_OP.slot,
//                    FUZZ_LAST_OP.size, FUZZ_LAST_OP.aux, FUZZ_LAST_OP.ptr);
//             __builtin_trap();
//         }
//     }
// }

// static void test_once(const uint8_t *input, size_t len)
// {
//     FR_Reader r = FR_init(input, len);

//     guards_init();
//     k_heap_init(&my_heap, g_heap_mem.mem, HEAP_SIZE);
//     memset(&g_slots.slots[0], 0, sizeof(g_slots.slots));
//     memset((void *)&FUZZ_LAST_OP, 0, sizeof(FUZZ_LAST_OP));
//     guards_check_or_trap();

//     uint32_t steps = FR_next_range(&r, 10, 50);

//     for (uint32_t i = 0; i < steps; i++) {
//         uint8_t action = FR_next_u8(&r) % 6;
//         uint8_t slot_idx = FR_next_u8(&r) % MAX_ALLOCS;
//         struct alloc_info *slot = &g_slots.slots[slot_idx];

//         FUZZ_LAST_OP.step = i;
//         FUZZ_LAST_OP.action = action;
//         FUZZ_LAST_OP.slot = slot_idx;
//         FUZZ_LAST_OP.size = 0;
//         FUZZ_LAST_OP.aux = 0;
//         FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;

//         switch (action) {
//         case 0: /* k_heap_alloc */
//             if (!slot->ptr) {
//                 slot->size = (size_t)FR_next_range(&r, 1, 512);
//                 slot->magic = FR_next_u8(&r);
//                 FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                 slot->ptr = k_heap_alloc(&my_heap, slot->size, K_NO_WAIT);
//                 FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                 if (slot->ptr) {
//                     scribble_memory(slot);
//                 }
//                 validate_heap_or_trap();
//                 guards_check_or_trap();
//             }
//             break;
//         case 1: /* k_heap_aligned_alloc */
//             if (!slot->ptr) {
//                 size_t align = 1u << FR_next_range(&r, 2, 6);
//                 slot->size = (size_t)FR_next_range(&r, 1, 512);
//                 slot->magic = FR_next_u8(&r);
//                 FUZZ_LAST_OP.aux = (uint32_t)align;
//                 FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                 slot->ptr = k_heap_aligned_alloc(&my_heap, align, slot->size, K_NO_WAIT);
//                 FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                 if (slot->ptr) {
//                     if (((uintptr_t)slot->ptr % align) != 0) {
//                         __builtin_trap();
//                     }
//                     scribble_memory(slot);
//                 }
//                 validate_heap_or_trap();
//                 guards_check_or_trap();
//             }
//             break;
//         case 2: /* k_heap_free */
//             if (slot->ptr) {
//                 FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                 FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                 verify_memory(slot);
//                 k_heap_free(&my_heap, slot->ptr);
//                 slot->ptr = NULL;
//                 FUZZ_LAST_OP.ptr = 0;
//                 validate_heap_or_trap();
//                 guards_check_or_trap();
//             }
//             break;
//         case 3: /* verify */
//             if (slot->ptr) {
//                 FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                 FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                 verify_memory(slot);
//             }
//             break;
//         case 4: /* k_heap_realloc */
//             if (slot->ptr) {
//                 FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                 verify_memory(slot);
//                 size_t new_size = (size_t)FR_next_range(&r, 0, 768);
//                 FUZZ_LAST_OP.size = (uint32_t)new_size;
//                 void *p2 = k_heap_realloc(&my_heap, slot->ptr, new_size, K_NO_WAIT);
//                 FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)p2;
//                 if (new_size == 0) {
//                     slot->ptr = NULL;
//                     slot->size = 0;
//                 } else if (p2 != NULL) {
//                     slot->ptr = p2;
//                     slot->size = new_size;
//                     scribble_memory(slot);
//                 }
//                 validate_heap_or_trap();
//                 guards_check_or_trap();
//             }
//             break;
//         case 5: /* k_heap_calloc */
//             if (!slot->ptr) {
//                 size_t nmemb = (size_t)FR_next_range(&r, 1, 32);
//                 size_t size = (size_t)FR_next_range(&r, 1, 32);
//                 size_t total;
//                 if (size_mul_overflow(nmemb, size, &total) || total > 512) {
//                     break;
//                 }
//                 FUZZ_LAST_OP.aux = (uint32_t)nmemb;
//                 FUZZ_LAST_OP.size = (uint32_t)total;
//                 void *p = k_heap_calloc(&my_heap, nmemb, size, K_NO_WAIT);
//                 FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)p;
//                 if (p) {
//                     uint8_t *check = (uint8_t *)p;
//                     for (size_t j = 0; j < total; j++) {
//                         if (check[j] != 0) {
//                             __builtin_trap();
//                         }
//                     }
//                     slot->ptr = p;
//                     slot->size = total;
//                     slot->magic = FR_next_u8(&r);
//                     scribble_memory(slot);
//                 }
//                 validate_heap_or_trap();
//                 guards_check_or_trap();
//             }
//             break;
//         }
//     }

//     for (int i = 0; i < MAX_ALLOCS; i++) {
//         if (g_slots.slots[i].ptr) {
//             k_heap_free(&my_heap, g_slots.slots[i].ptr);
//             g_slots.slots[i].ptr = NULL;
//         }
//     }
//     validate_heap_or_trap();
//     guards_check_or_trap();

//     if (FUZZ_PRINT_LAST_OP) {
//         printk("[LAST_OP] step=%u action=%u slot=%u size=%u aux=%u ptr=0x%08x\n",
//                FUZZ_LAST_OP.step, FUZZ_LAST_OP.action, FUZZ_LAST_OP.slot,
//                FUZZ_LAST_OP.size, FUZZ_LAST_OP.aux, FUZZ_LAST_OP.ptr);
//     }
// }

// int main(void)
// {
//     /* Keep a live reference to the padding so it can't be optimized away. */
//     FUZZ_PAD[0] = FUZZ_PAD[0];

//     test_once(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
//     printk("[TEST_CASE_COMPLETED]\n");
//     k_msleep(1);
//     volatile int bp = BREAKPOINT();
//     (void)bp;
//     return 0;
// }
/*

 * Zephyr k_heap 强化版模糊测试 Harness (物理隔离 + 单次执行版)

 */



/*
 * Zephyr k_heap 强化版模糊测试 Harness
 * 优化重点：内存染色、对齐验证、完整性校验、多 API 覆盖
 */

// #include <zephyr/kernel.h>
// #include <zephyr/sys/printk.h>
// #include <zephyr/sys/math_extras.h>
// #include <zephyr/sys/sys_heap.h>
// #include <zephyr/sys/util.h>
// #include <string.h>
// #include <stdint.h>

// // --- LibAFL 配置 ---
// #define MAX_FUZZ_INPUT_SIZE 1024
// /*
//  * Some runners use a "magic" MMIO address at the start of SRAM.
//  * Avoid placing FUZZ_INPUT at 0x20000000 by inserting a small .bss padding.
//  */
// __attribute__((used, section(".bss.00_fuzz_pad")))
// volatile uint8_t FUZZ_PAD[512];

// __attribute__((used, visibility("default"), section(".bss.10_fuzz_input")))
// unsigned char FUZZ_INPUT[MAX_FUZZ_INPUT_SIZE];

// struct fuzz_last_op {
//     uint32_t step;
//     uint32_t action;
//     uint32_t slot;
//     uint32_t size;
//     uint32_t aux;
//     uint32_t ptr;
// };

// __attribute__((used, visibility("default")))
// volatile struct fuzz_last_op FUZZ_LAST_OP;

// __attribute__((used, visibility("default")))
// volatile uint32_t FUZZ_PRINT_LAST_OP;

// /*
//  * Ensure FUZZ_INPUT is retained in the final image even when building
//  * in non-fuzz modes (e.g., HEAP_SELFTEST_ONLY) where it might otherwise
//  * be removed by aggressive GC/LTO.
//  */
// static volatile uintptr_t fuzz_input_anchor;

// // --- 测试配置 ---
// #define MAX_ALLOCS 12      // 增加指针池大小以提升碎片化复杂度
// #define HEAP_SIZE 4096     // 扩大堆空间
// #define MIN_ALIGN 4
// #define MAX_ALIGN 64

// static uint8_t heap_mem[HEAP_SIZE] __aligned(64);
// static struct k_heap my_heap;

// // 记录分配信息的结构体，用于检测 OOB (越界)
// struct alloc_info {
//     void *ptr;
//     size_t size;
//     uint8_t magic; // 用于校验内存内容的幻数
// };

// static struct alloc_info slots[MAX_ALLOCS];

// static inline void validate_heap_or_trap(void)
// {
//     if (!sys_heap_validate(&my_heap.heap)) {
//         printk("FATAL: sys_heap_validate failed\n");
//         printk("[LAST_OP] step=%u action=%u slot=%u size=%u aux=%u ptr=0x%08x\n",
//                FUZZ_LAST_OP.step, FUZZ_LAST_OP.action, FUZZ_LAST_OP.slot,
//                FUZZ_LAST_OP.size, FUZZ_LAST_OP.aux, FUZZ_LAST_OP.ptr);
//         __builtin_trap();
//     }
// }

// // --- 基础辅助工具 (FR_Reader) ---
// typedef struct {
//     const unsigned char* data;
//     size_t size;
//     size_t off;
// } FR_Reader;

// static inline FR_Reader FR_init(const unsigned char* buf, size_t n) {
//     return (FR_Reader){ buf, n, 0 };
// }

// static inline uint8_t FR_next_u8(FR_Reader* r) {
//     return (r->off < r->size) ? r->data[r->off++] : 0;
// }

// static inline uint32_t FR_next_u32(FR_Reader* r) {
//     uint32_t v = 0;
//     for (int i = 0; i < 4; i++) v |= ((uint32_t)FR_next_u8(r) << (i * 8));
//     return v;
// }

// static inline uint32_t FR_next_range(FR_Reader* r, uint32_t min, uint32_t max) {
//     if (max <= min) return min;
//     return min + (FR_next_u32(r) % (max - min + 1));
// }

// // --- 强化功能函数 ---

// // 1. 内存填充：向分配的块写入特定模式，用于检测非法篡改
// static void scribble_memory(struct alloc_info *info) {
//     if (info->ptr && info->size > 0) {
//         memset(info->ptr, info->magic, info->size);
//     }
// }

// // 2. 内存校验：释放前检查内容是否被意外修改
// static void verify_memory(struct alloc_info *info) {
//     if (!info->ptr) return;
//     uint8_t *p = (uint8_t *)info->ptr;
//     for (size_t i = 0; i < info->size; i++) {
//         if (p[i] != info->magic) {
//             printk("FATAL: Memory corruption detected at %p (offset %zu)\n", p, i);
//             printk("[LAST_OP] step=%u action=%u slot=%u size=%u aux=%u ptr=0x%08x\n",
//                    FUZZ_LAST_OP.step, FUZZ_LAST_OP.action, FUZZ_LAST_OP.slot,
//                    FUZZ_LAST_OP.size, FUZZ_LAST_OP.aux, FUZZ_LAST_OP.ptr);
//             __builtin_trap(); // 强制触发 Fuzzer 崩溃捕获
//         }
//     }
// }

// // 3. 停机指令
// int __attribute__((noinline, used, visibility("default"))) BREAKPOINT(void)
// {
//     __asm__ volatile("" ::: "memory");
//     return 0;
// }

// // --- 核心测试函数 ---
// void test_once(const uint8_t *input, size_t len) {
//     FR_Reader r = FR_init(input, len);
    
//     // 重置状态
//     k_heap_init(&my_heap, heap_mem, HEAP_SIZE);
//     memset(slots, 0, sizeof(slots));
//     validate_heap_or_trap();

//     // 从输入决定执行多少步操作 (10-50步)
//     uint32_t steps = FR_next_range(&r, 10, 50);

//     for (uint32_t i = 0; i < steps; i++) {
//         uint8_t action = FR_next_u8(&r) % 6; // 六种动作
//         uint8_t slot_idx = FR_next_u8(&r) % MAX_ALLOCS;
//         struct alloc_info *slot = &slots[slot_idx];

//         FUZZ_LAST_OP.step = i;
//         FUZZ_LAST_OP.action = action;
//         FUZZ_LAST_OP.slot = slot_idx;
//         FUZZ_LAST_OP.size = 0;
//         FUZZ_LAST_OP.aux = 0;
//         FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;

//         switch (action) {
//             case 0: // 常规分配 (k_heap_alloc)
//                 if (!slot->ptr) {
//                     slot->size = (size_t)FR_next_range(&r, 1, 512);
//                     slot->magic = FR_next_u8(&r);
//                     FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                     slot->ptr = k_heap_alloc(&my_heap, slot->size, K_NO_WAIT);
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                     scribble_memory(slot);
//                     validate_heap_or_trap();
//                 }
//                 break;

//             case 1: // 对齐分配 (k_heap_aligned_alloc)
//                 if (!slot->ptr) {
//                     // 随机对齐量：4, 8, 16, 32, 64
//                     size_t align = 1 << FR_next_range(&r, 2, 6); 
//                     slot->size = (size_t)FR_next_range(&r, 1, 512);
//                     slot->magic = FR_next_u8(&r);
//                     FUZZ_LAST_OP.aux = (uint32_t)align;
//                     FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                     slot->ptr = k_heap_aligned_alloc(&my_heap, align, slot->size, K_NO_WAIT);
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
                    
//                     // 验证对齐是否生效
//                     if (slot->ptr && ((uintptr_t)slot->ptr % align != 0)) {
//                         printk("FATAL: Alignment error! ptr=%p, align=%zu\n", slot->ptr, align);
//                         __builtin_trap();
//                     }
//                     scribble_memory(slot);
//                     validate_heap_or_trap();
//                 }
//                 break;

//             case 2: // 释放内存 (k_heap_free)
//                 if (slot->ptr) {
//                     FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                     verify_memory(slot); // 释放前校验，捕获静默越界
//                     k_heap_free(&my_heap, slot->ptr);
//                     slot->ptr = NULL;
//                     FUZZ_LAST_OP.ptr = 0;
//                     validate_heap_or_trap();
//                 }
//                 break;

//             case 3: // 模拟“使用中”读取
//                 if (slot->ptr) {
//                     FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                     verify_memory(slot);
//                 }
//                 break;

//             case 4: // 重新分配 (k_heap_realloc)
//                 if (slot->ptr) {
//                     FUZZ_LAST_OP.size = (uint32_t)slot->size;
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)slot->ptr;
//                     verify_memory(slot);
//                     size_t new_size = (size_t)FR_next_range(&r, 0, 768);
//                     FUZZ_LAST_OP.size = (uint32_t)new_size;
//                     void *p2 = k_heap_realloc(&my_heap, slot->ptr, new_size, K_NO_WAIT);
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)p2;
//                     if (new_size == 0) {
//                         /* realloc(ptr,0) frees and returns NULL */
//                         if (p2 != NULL) {
//                             printk("FATAL: realloc(ptr,0) returned non-NULL\n");
//                             __builtin_trap();
//                         }
//                         slot->ptr = NULL;
//                         slot->size = 0;
//                     } else if (p2 != NULL) {
//                         slot->ptr = p2;
//                         slot->size = new_size;
//                         scribble_memory(slot);
//                     } else {
//                         /* realloc failed: original pointer remains valid */
//                         verify_memory(slot);
//                     }
//                     validate_heap_or_trap();
//                 }
//                 break;

//             case 5: // calloc (k_heap_calloc)
//                 if (!slot->ptr) {
//                     size_t n = (size_t)FR_next_range(&r, 1, 64);
//                     size_t sz = (size_t)FR_next_range(&r, 1, 64);
//                     size_t total;
//                     if (size_mul_overflow(n, sz, &total) || total == 0 || total > 512) {
//                         break;
//                     }

//                     FUZZ_LAST_OP.aux = (uint32_t)n;
//                     FUZZ_LAST_OP.size = (uint32_t)total;

//                     void *p = k_heap_calloc(&my_heap, n, sz, K_NO_WAIT);
//                     FUZZ_LAST_OP.ptr = (uint32_t)(uintptr_t)p;
//                     if (p != NULL) {
//                         /* Verify calloc semantics for requested bytes only */
//                         uint8_t *b = (uint8_t *)p;
//                         for (size_t j = 0; j < total; j++) {
//                             if (b[j] != 0) {
//                                 printk("FATAL: calloc not zeroed (offset %zu)\n", j);
//                                 __builtin_trap();
//                             }
//                         }

//                         slot->ptr = p;
//                         slot->size = total;
//                         slot->magic = FR_next_u8(&r);
//                         scribble_memory(slot);
//                         validate_heap_or_trap();
//                     }
//                 }
//                 break;
//         }
//     }

//     // 清理：释放所有遗留内存，防止干扰下一轮测试
//     for (int i = 0; i < MAX_ALLOCS; i++) {
//         if (slots[i].ptr) {
//             verify_memory(&slots[i]);
//             k_heap_free(&my_heap, slots[i].ptr);
//         }
//     }

//     validate_heap_or_trap();
    
//     if (FUZZ_PRINT_LAST_OP) {
//         printk("[LAST_OP] step=%u action=%u slot=%u size=%u aux=%u ptr=0x%08x\n",
//                FUZZ_LAST_OP.step, FUZZ_LAST_OP.action, FUZZ_LAST_OP.slot,
//                FUZZ_LAST_OP.size, FUZZ_LAST_OP.aux, FUZZ_LAST_OP.ptr);
//     }

//     printk("[TEST_CASE_COMPLETED]\n");
// }

// int main(void) {
//     /* Keep a runtime reference to FUZZ_INPUT for external runners. */
//     fuzz_input_anchor = (uintptr_t)FUZZ_INPUT;
// #ifndef HEAP_SELFTEST_ONLY
//     /* Keep a runtime reference to FUZZ_PAD so it can't be optimized away. */
//     FUZZ_PAD[0] = FUZZ_PAD[0];
// #endif
// #ifdef HEAP_SELFTEST_ONLY
//     extern void heap_selftest_run(void);
//     heap_selftest_run();
//     printk("[SELFTEST_DONE]\n");
//     k_msleep(1);
//     volatile int bp = BREAKPOINT();
//     (void)bp;
//     return 0;
// #else
//     while (1) {
//         test_once(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
//         k_msleep(1);
//         volatile int bp = BREAKPOINT();
//         (void)bp;
//     }
// #endif
// }

/*
 * Zephyr k_heap 强化版模糊测试 Harness (单次执行 + 物理隔离)
 */

/*
 * Zephyr k_heap 深度压力探索版 Harness
 */

/*
 * Zephyr k_heap 深度压力探索版 Harness (修正版)
 * 目标：在排除 Harness 自身 Bug 的前提下，探测内核堆管理竞态漏洞
 */

// #include <zephyr/kernel.h>
// #include <zephyr/sys/printk.h>
// #include <zephyr/sys/math_extras.h>
// #include <zephyr/sys/sys_heap.h>
// #include <zephyr/sys/util.h>

// #include <stdbool.h>
// #include <stdint.h>
// #include <string.h>

// #define MAX_FUZZ_INPUT_SIZE 1024
// #define MAX_ALLOCS 16

// /*
//  * Runner-compat: avoid placing FUZZ_INPUT at 0x20000000 by inserting a padding
//  * object that sorts earlier in .bss.
//  */
// __attribute__((used, section(".bss.00_fuzz_pad")))
// volatile uint8_t FUZZ_PAD[512];

// __attribute__((used, visibility("default"), section(".bss.10_fuzz_input")))
// unsigned char FUZZ_INPUT[MAX_FUZZ_INPUT_SIZE];

// struct fuzz_last_op {
//     uint32_t epoch;
//     uint32_t step;
//     uint32_t action;
//     uint32_t slot;
//     uint32_t size;
//     uint32_t aux;
//     uint32_t ptr;
// };

// __attribute__((used, visibility("default")))
// volatile struct fuzz_last_op FUZZ_LAST_OP;

// __attribute__((used, visibility("default")))
// volatile uint32_t FUZZ_PRINT_LAST_OP;

int __attribute__((noinline, used, visibility("default"))) BREAKPOINT(void)
{
    __asm__ volatile("" ::: "memory");
    return 0;
}


/*
 * Zephyr k_heap 0-day 探测与证据提取 Harness (修正优先级版)
 */

// #include <zephyr/kernel.h>
// #include <zephyr/sys/printk.h>
// #include <zephyr/sys/math_extras.h>
// #include <zephyr/sys/sys_heap.h>
// #include <string.h>

// #define MAX_FUZZ_INPUT_SIZE 1024
// #define GUARD_SIZE 32
// #define GUARD_PATTERN 0xAA
// #define HEAP_SIZE 4096 
// #define MAX_ALLOCS 16

// // --- 1. 核心防御与检查代码 ---
// static uint8_t _guard_heap_pre[GUARD_SIZE] __aligned(64);
// static uint8_t heap_mem[HEAP_SIZE] __aligned(64);
// static uint8_t _guard_heap_post[GUARD_SIZE] __aligned(64);

// static void check_guard_zones(const char *tag) {
//     for (int i = 0; i < GUARD_SIZE; i++) {
//         if (_guard_heap_pre[i] != GUARD_PATTERN) {
//             printk("\n[!] CRITICAL: PRE-GUARD BREACHED during %s!\n", tag);
//             __builtin_trap();
//         }
//         if (_guard_heap_post[i] != GUARD_PATTERN) {
//             printk("\n[!] CRITICAL: POST-GUARD BREACHED during %s!\n", tag);
//             __builtin_trap();
//         }
//     }
// }

// static void init_guards(void) {
//     memset(_guard_heap_pre, GUARD_PATTERN, GUARD_SIZE);
//     memset(_guard_heap_post, GUARD_PATTERN, GUARD_SIZE);
// }

// // --- 2. 变量定义 ---
// struct alloc_info {
//     void *ptr;
//     size_t size;
//     uint8_t magic;
// };
// static struct alloc_info slots[MAX_ALLOCS];
// static struct k_heap my_heap;
// K_SEM_DEFINE(fuzz_sem, 1, 1);
// static volatile bool fuzz_active = false;

// __attribute__((used, visibility("default"))) 
// unsigned char FUZZ_INPUT[MAX_FUZZ_INPUT_SIZE] = {0x90, 0xa0, 0xb0, 0x7f, 0x5f};

// // --- 3. 后台竞争线程 ---
// void background_worker(void *p1, void *p2, void *p3) {
//     while (1) {
//         if (fuzz_active && k_sem_take(&fuzz_sem, K_NO_WAIT) == 0) {
//             void *p = k_heap_alloc(&my_heap, 8, K_NO_WAIT);
//             if (p) k_heap_free(&my_heap, p);
//             k_sem_give(&fuzz_sem);
//         }
//         k_yield();
//     }
// }

// /* * 关键修正：优先级从 15 改为 14
//  * 确保符合 CONFIG_NUM_PREEMPT_PRIORITIES 的限制
//  */
// K_THREAD_DEFINE(worker_tid, 1024, background_worker, NULL, NULL, NULL, 14, 0, 0);

// // --- 4. 辅助工具 ---
// typedef struct { const unsigned char* data; size_t size; size_t off; } FR_Reader;
// static inline uint8_t FR_next_u8(FR_Reader* r) { return (r->off < r->size) ? r->data[r->off++] : 0; }
// static inline uint32_t FR_next_u32(FR_Reader* r) { 
//     uint32_t v = 0; 
//     for (int i = 0; i < 4; i++) v |= ((uint32_t)FR_next_u8(r) << (i * 8)); 
//     return v; 
// }

// // --- 5. 核心测试逻辑 ---
// void test_once(const uint8_t *input, size_t len) {
//     FR_Reader r = { .data = input, .size = len, .off = 0 };

//     k_sem_take(&fuzz_sem, K_FOREVER);
//     fuzz_active = false;
//     init_guards(); 
    
//     // 初始化堆
//     k_heap_init(&my_heap, heap_mem, HEAP_SIZE);
//     check_guard_zones("k_heap_init"); 
    
//     memset(slots, 0, sizeof(slots));
//     fuzz_active = true;
//     k_sem_give(&fuzz_sem);

//     uint32_t steps = 100;

//     for (uint32_t i = 0; i < steps; i++) {
//         uint8_t action = FR_next_u8(&r) % 6;
//         uint8_t slot_idx = FR_next_u8(&r) % MAX_ALLOCS;
//         struct alloc_info *slot = &slots[slot_idx];

//         k_sem_take(&fuzz_sem, K_FOREVER);
        
//         switch (action) {
//             case 0: // Alloc
//                 if (!slot->ptr) {
//                     slot->size = (FR_next_u8(&r) % 512) + 1;
//                     slot->magic = FR_next_u8(&r);
//                     slot->ptr = k_heap_alloc(&my_heap, slot->size, K_NO_WAIT);
//                     if (slot->ptr) memset(slot->ptr, slot->magic, slot->size);
//                 }
//                 break;
//             case 1: // Free
//                 if (slot->ptr) {
//                     k_heap_free(&my_heap, slot->ptr);
//                     slot->ptr = NULL;
//                 }
//                 break;
//             case 2: // Realloc
//                 if (slot->ptr) {
//                     size_t ns = FR_next_u8(&r) * 4;
//                     void *p2 = k_heap_realloc(&my_heap, slot->ptr, ns, K_NO_WAIT);
//                     if (ns == 0) slot->ptr = NULL;
//                     else if (p2) { slot->ptr = p2; slot->size = ns; }
//                 }
//                 break;
//             case 3: // Aligned Alloc
//                 if (!slot->ptr) {
//                     size_t sz = (FR_next_u8(&r) % 128) + 1;
//                     slot->ptr = k_heap_aligned_alloc(&my_heap, 16, sz, K_NO_WAIT);
//                     if (slot->ptr) {
//                         slot->size = sz;
//                         slot->magic = 0xEE;
//                         memset(slot->ptr, slot->magic, sz);
//                     }
//                 }
//                 break;
//         }

//         if (!sys_heap_validate(&my_heap.heap)) __builtin_trap();
//         check_guard_zones("heap_operation"); 

//         k_sem_give(&fuzz_sem);
//         if (i % 10 == 0) k_yield();
//     }

//     k_sem_take(&fuzz_sem, K_FOREVER);
//     fuzz_active = false;
//     for (int i = 0; i < MAX_ALLOCS; i++) {
//         if (slots[i].ptr) k_heap_free(&my_heap, slots[i].ptr);
//     }
//     check_guard_zones("final_cleanup");
//     k_sem_give(&fuzz_sem);
// }

// int main(void) {
//     printk("[0-DAY CHECK START]\n");
//     test_once(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
//     printk("[0-DAY CHECK COMPLETED - NO BREACH]\n");
//     k_msleep(10);
//      volatile int bp = BREAKPOINT();
//     return 0;
// }
/*
 * Zephyr k_heap 深度压力探测版 Harness (整合版)
 * 目标：探测多线程竞态、深度碎片化以及边缘尺寸处理中的 0-day 漏洞
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/math_extras.h>
#include <zephyr/sys/sys_heap.h>
#include <string.h>

#define MAX_FUZZ_INPUT_SIZE 1024
#define GUARD_SIZE 32
#define GUARD_PATTERN 0xAA
#define HEAP_SIZE 2048  // 缩小堆空间，更容易触发“满堆”和“碎片合并”逻辑
#define MAX_ALLOCS 16

// --- 1. 物理隔离与染色 ---
static uint8_t _guard_heap_pre[GUARD_SIZE] __aligned(64);
static uint8_t heap_mem[HEAP_SIZE] __aligned(64);
static uint8_t _guard_heap_post[GUARD_SIZE] __aligned(64);

// 2. 遥测与同步
struct fuzz_last_op {
    uint32_t step;
    uint32_t action;
    uint32_t slot;
    uint32_t size;
};
__attribute__((used, visibility("default"))) 
volatile struct fuzz_last_op FUZZ_LAST_OP;

K_SEM_DEFINE(fuzz_sem, 1, 1);
static volatile bool fuzz_active = false;

struct alloc_info {
    void *ptr;
    size_t size;
    uint8_t magic;
};
static struct alloc_info slots[MAX_ALLOCS];
static struct k_heap my_heap;

__attribute__((used, visibility("default"))) 
unsigned char FUZZ_INPUT[MAX_FUZZ_INPUT_SIZE] = {0x90, 0xa0, 0xb0, 0x7f, 0x5f};

// --- 3. 校验工具 ---
static void check_guards(const char *tag) {
    for (int i = 0; i < GUARD_SIZE; i++) {
        if (_guard_heap_pre[i] != GUARD_PATTERN || _guard_heap_post[i] != GUARD_PATTERN) {
            printk("\n[!] GUARD BREACHED during %s! i=%d\n", tag, i);
            __builtin_trap();
        }
    }
}

static inline void validate_all(void) {
    if (!sys_heap_validate(&my_heap.heap)) __builtin_trap();
    check_guards("runtime_op");
}

// --- 4. 后台竞态线程 ---
void background_worker(void *p1, void *p2, void *p3) {
    while (1) {
        if (fuzz_active && k_sem_take(&fuzz_sem, K_NO_WAIT) == 0) {
            void *p = k_heap_alloc(&my_heap, 4, K_NO_WAIT);
            if (p) k_heap_free(&my_heap, p);
            k_sem_give(&fuzz_sem);
        }
        k_yield();
    }
}
K_THREAD_DEFINE(worker_tid, 1024, background_worker, NULL, NULL, NULL, 14, 0, 0);

// --- 5. FR_Reader ---
typedef struct { const unsigned char* data; size_t size; size_t off; } FR_Reader;
static inline uint8_t FR_next_u8(FR_Reader* r) { return (r->off < r->size) ? r->data[r->off++] : 0; }
static inline uint32_t FR_next_u32(FR_Reader* r) { 
    uint32_t v = 0; 
    for (int i = 0; i < 4; i++) v |= ((uint32_t)FR_next_u8(r) << (i * 8)); 
    return v; 
}
static inline uint32_t FR_next_range(FR_Reader* r, uint32_t min, uint32_t max) {
    if (max <= min) return min;
    return min + (FR_next_u32(r) % (max - min + 1));
}

// --- 6. 核心测试函数 ---
void test_once(const uint8_t *input, size_t len) {
    FR_Reader r = { .data = input, .size = len, .off = 0 };

    // A. 初始化与加锁
    k_sem_take(&fuzz_sem, K_FOREVER);
    fuzz_active = false;
    memset(_guard_heap_pre, GUARD_PATTERN, GUARD_SIZE);
    memset(_guard_heap_post, GUARD_PATTERN, GUARD_SIZE);
    memset(slots, 0, sizeof(slots));

    k_heap_init(&my_heap, heap_mem, HEAP_SIZE);
    check_guards("k_heap_init");
    
    fuzz_active = true;
    k_sem_give(&fuzz_sem);

    // 增加步数上限以探测更深层的状态
    uint32_t steps = FR_next_range(&r, 50, 250); 

    for (uint32_t i = 0; i < steps; i++) {
        uint8_t action = FR_next_u8(&r) % 8; // 8 种探测行为
        uint8_t slot_idx = FR_next_u8(&r) % MAX_ALLOCS;
        struct alloc_info *slot = &slots[slot_idx];

        k_sem_take(&fuzz_sem, K_FOREVER);
        
        FUZZ_LAST_OP.step = i;
        FUZZ_LAST_OP.action = action;
        FUZZ_LAST_OP.slot = slot_idx;

        switch (action) {
            case 0: // 常规 Alloc
                if (!slot->ptr) {
                    slot->size = FR_next_range(&r, 1, 512);
                    slot->magic = FR_next_u8(&r);
                    slot->ptr = k_heap_alloc(&my_heap, slot->size, K_NO_WAIT);
                    if (slot->ptr) memset(slot->ptr, slot->magic, slot->size);
                }
                break;

            case 1: // 常规 Free
                if (slot->ptr) {
                    k_heap_free(&my_heap, slot->ptr);
                    slot->ptr = NULL;
                }
                break;

            case 2: // 极限 Realloc
                if (slot->ptr) {
                    // 包含 0, 1, 以及接近堆上限的临界尺寸
                    uint32_t sizes[] = {0, 1, 8, 64, 512, 1024, 2047};
                    size_t ns = sizes[FR_next_u8(&r) % 7];
                    void *p2 = k_heap_realloc(&my_heap, slot->ptr, ns, K_NO_WAIT);
                    if (ns == 0) slot->ptr = NULL;
                    else if (p2) {
                        slot->ptr = p2;
                        slot->size = ns;
                        memset(slot->ptr, slot->magic, ns);
                    }
                }
                break;

            case 3: // 变态对齐分配
                if (!slot->ptr) {
                    uint32_t aligns[] = {4, 8, 16, 32, 64, 128, 256};
                    size_t al = aligns[FR_next_u8(&r) % 7];
                    size_t sz = FR_next_range(&r, 1, 256);
                    slot->ptr = k_heap_aligned_alloc(&my_heap, al, sz, K_NO_WAIT);
                    if (slot->ptr) {
                        slot->size = sz;
                        slot->magic = 0xCC;
                        memset(slot->ptr, slot->magic, sz);
                    }
                }
                break;

            case 4: // 碎片制造机 (Fragmenter)
                // 在堆中制造大量不可回收的小空洞
                for (int j = 0; j < 3; j++) {
                    void *p = k_heap_alloc(&my_heap, 4, K_NO_WAIT);
                    // 只有一部分会被立即释放
                    if (p && (FR_next_u8(&r) % 2 == 0)) k_heap_free(&my_heap, p);
                }
                break;

            case 5: // 数据完整性校验
                if (slot->ptr && slot->size > 0) {
                    uint8_t *p = (uint8_t *)slot->ptr;
                    if (p[0] != slot->magic) __builtin_trap();
                }
                break;

            case 6: // “邻居合并”诱导
                // 专门释放上一个槽位，强迫算法执行 Merge
                if (slot_idx > 0 && slots[slot_idx-1].ptr) {
                    k_heap_free(&my_heap, slots[slot_idx-1].ptr);
                    slots[slot_idx-1].ptr = NULL;
                }
                break;

            case 7: // 调度器压力
                k_sem_give(&fuzz_sem);
                k_yield(); // 诱发 worker 线程抢占堆锁
                k_sem_take(&fuzz_sem, K_FOREVER);
                break;
        }

        validate_all();
        k_sem_give(&fuzz_sem);
    }

    // 轮次清理
    k_sem_take(&fuzz_sem, K_FOREVER);
    fuzz_active = false;
    for (int i = 0; i < MAX_ALLOCS; i++) {
        if (slots[i].ptr) k_heap_free(&my_heap, slots[i].ptr);
    }
    k_sem_give(&fuzz_sem);
}

int main(void) {
    printk("[DEEP_FUZZ_START]\n");
    test_once(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
    printk("[DEEP_FUZZ_COMPLETED]\n");
    k_msleep(20);
     volatile int bp = BREAKPOINT();
    return 0;
}