/*
 * Zephyr fuzz reproduction harness
 * Crash Input: 0c4aae4321fe8974 (Reproduction Mode)
 * Status: Fixed Reader Logic & Enabled Padding Check
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_heap.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

// LibAFL integration: deterministic global input buffer
#define MAX_FUZZ_INPUT_SIZE 1024

// 已填入你的 Crash Data (72 bytes)
__attribute__((used, visibility("default"))) unsigned char FUZZ_INPUT[MAX_FUZZ_INPUT_SIZE] = {
    // 00000000
    0xd3, 0x2d, 0xff, 0xbb, 0xbc, 0x45, 0xd3, 0xd3, 0x01, 0x00, 0xed, 0x10, 0xed, 0x6d, 0xed, 0xed,
    // 00000010
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0xc5, 0xc5, 0xc5, 0xc5, 0xc5, 0xc5, 0xff, 0xed, 0xed,
    // 00000020
    0xff, 0xed, 0x10, 0xed, 0xff, 0xed, 0x10, 0xed, 0x6d, 0xed, 0x10, 0x0d, 0xed, 0xff, 0x13, 0x10,
    // 00000030
    0xed, 0xff, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0xd3, 0xde,
    // 00000040
    0xd3, 0xd3, 0x4f, 0xa5, 0xd3, 0x4f, 0xed, 0xed
};

int __attribute__((noinline, used, visibility("default"))) BREAKPOINT(void)
{
    for (;;) {
        __asm volatile("nop");
    }
}

typedef struct {
    const unsigned char* data;
    size_t size;
    size_t off;
} FR_Reader;

static inline FR_Reader FR_init(const unsigned char* buf, size_t n)
{
    FR_Reader r = { buf, n, 0 };
    return r;
}

static inline size_t FR_remaining(FR_Reader* r)
{
    return (r->off < r->size) ? (r->size - r->off) : 0;
}

static inline uint8_t FR_next_u8(FR_Reader* r)
{
    if (r->off + 1 <= r->size) {
        return r->data[r->off++];
    }
    return 0;
}

static inline uint16_t FR_next_u16(FR_Reader* r)
{
    uint16_t lo = FR_next_u8(r);
    uint16_t hi = FR_next_u8(r);
    return (uint16_t)((hi << 8) | lo);
}

static inline uint32_t FR_next_u32(FR_Reader* r)
{
    uint32_t lo = FR_next_u16(r);
    uint32_t hi = FR_next_u16(r);
    return (hi << 16) | lo;
}

static inline int32_t FR_next_s32(FR_Reader* r)
{
    return (int32_t)FR_next_u32(r);
}

static inline uint32_t FR_next_range(FR_Reader* r, uint32_t min_v, uint32_t max_v)
{
    if (max_v <= min_v) {
        return min_v;
    }
    uint32_t span = max_v - min_v + 1u;
    return min_v + (FR_next_u32(r) % span);
}

static inline size_t FR_next_bytes(FR_Reader* r, unsigned char* out, size_t n)
{
    size_t rem = FR_remaining(r);
    if (n > rem) {
        n = rem;
    }
    if (n) {
        memcpy(out, r->data + r->off, n);
        r->off += n;
    }
    return n;
}

// -------------------------------------------------------------

#define MAX_ALLOCS 8
#define HEAP_SIZE 2048

#define PAD_WORDS 1024
#define PAD_PRE_PATTERN  0xA5A5A5A5u
#define PAD_POST_PATTERN 0x5A5A5A5Au

// 【重要】恢复 Padding 布局，以便用 GDB 证明是“隔空打击”
struct safe_heap_layout {
    volatile uint32_t padding_pre[PAD_WORDS];  // 4KB 前置装甲
    uint8_t heap_mem[HEAP_SIZE];          // 核心堆内存
    volatile uint32_t padding_post[PAD_WORDS]; // 4KB 后置装甲
} __attribute__((aligned(8)));

static struct safe_heap_layout my_layout;
static struct k_heap my_heap;
static void *ptrs[MAX_ALLOCS];

static inline void die_now(void)
{
    /* Crash/stop fast for the fuzzer. */
    k_panic();
}

static void padding_init(void)
{
    for (size_t i = 0; i < PAD_WORDS; ++i) {
        my_layout.padding_pre[i] = PAD_PRE_PATTERN;
        my_layout.padding_post[i] = PAD_POST_PATTERN;
    }
}

static bool padding_ok(void)
{
    /* Full scan is fine for debugging; tighten later if needed. */
    for (size_t i = 0; i < PAD_WORDS; ++i) {
        if (my_layout.padding_pre[i] != PAD_PRE_PATTERN) {
            return false;
        }
        if (my_layout.padding_post[i] != PAD_POST_PATTERN) {
            return false;
        }
    }
    return true;
}

static inline bool heap_ok(void)
{
    return sys_heap_validate(&my_heap.heap);
}

static inline bool ptr_in_heap(void *p)
{
    uintptr_t addr = (uintptr_t)p;
    uintptr_t start = (uintptr_t)my_layout.heap_mem;
    uintptr_t end = start + (uintptr_t)HEAP_SIZE;
    return (addr >= start) && (addr < end);
}

static void check_invariants_or_die(const char *where)
{
    if (!padding_ok()) {
        printk("[BUG] padding corrupted at %s\n", where);
        die_now();
    }
    if (!heap_ok()) {
        printk("[BUG] heap validation failed at %s\n", where);
        die_now();
    }
}

static inline size_t min_size(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

static inline bool mul_overflow_size(size_t a, size_t b, size_t *out)
{
    if (a == 0 || b == 0) {
        *out = 0;
        return false;
    }
    if (a > (SIZE_MAX / b)) {
        return true;
    }
    *out = a * b;
    return false;
}

static void test_once(void)
{
    /*
     * Per-execution harness:
     * - Always re-init heap and padding to avoid cross-run state.
     * - Validate heap and guard bands aggressively to pinpoint first corruption.
     */
    padding_init();
    k_heap_init(&my_heap, my_layout.heap_mem, HEAP_SIZE);
    for (int j = 0; j < MAX_ALLOCS; j++) {
        ptrs[j] = NULL;
    }
    check_invariants_or_die("init");

    FR_Reader reader = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    /* Consume a small baseline/prefix to decorrelate from fixed headers. */
    unsigned char baseline[8] = {0};
    (void)FR_next_bytes(&reader, baseline, sizeof(baseline));

    /* Steps per input. Allow enough operations to trigger fragmentation bugs. */
    uint32_t steps = FR_next_range(&reader, 1, 256);

    for (uint32_t i = 0; i < steps; ++i) {
        if (FR_remaining(&reader) < 8) {
            break;
        }

        uint32_t action = FR_next_range(&reader, 0, 2);
        uint32_t slot = FR_next_range(&reader, 0, MAX_ALLOCS - 1);

        if (action == 0) {
            /* alloc */
            if (FR_remaining(&reader) < 4) {
                break;
            }
            size_t sz = (size_t)FR_next_range(&reader, 1, HEAP_SIZE);
            if (ptrs[slot] == NULL) {
                void *p = k_heap_alloc(&my_heap, sz, K_NO_WAIT);
                if (p != NULL) {
                    if (!ptr_in_heap(p)) {
                        printk("[BUG] alloc returned non-heap ptr %p\n", p);
                        die_now();
                    }
                    /* Touch a few bytes to exercise the allocated region safely. */
                    size_t usable = sys_heap_usable_size(&my_heap.heap, p);
                    size_t touch = min_size(usable, 16);
                    memset(p, 0xA5, touch);
                    ptrs[slot] = p;
                }
            }
            check_invariants_or_die("alloc");
        } else if (action == 1) {
            /* calloc */
            if (FR_remaining(&reader) < 8) {
                break;
            }
            size_t n = (size_t)FR_next_range(&reader, 1, 32);
            size_t s = (size_t)FR_next_range(&reader, 1, 256);
            if (ptrs[slot] == NULL) {
                void *p = k_heap_calloc(&my_heap, n, s, K_NO_WAIT);
                if (p != NULL) {
                    if (!ptr_in_heap(p)) {
                        printk("[BUG] calloc returned non-heap ptr %p\n", p);
                        die_now();
                    }
                    /*
                     * calloc() guarantees zeroing of the requested n*s bytes,
                     * not necessarily the whole allocator-usable size.
                     */
                    size_t requested = 0;
                    if (mul_overflow_size(n, s, &requested)) {
                        printk("[BUG] calloc size overflow (n=%u s=%u)\n", (unsigned)n, (unsigned)s);
                        die_now();
                    }
                    size_t usable = sys_heap_usable_size(&my_heap.heap, p);
                    size_t touch = min_size(min_size(requested, usable), 16);
                    for (size_t k = 0; k < touch; ++k) {
                        if (((uint8_t *)p)[k] != 0) {
                            printk("[BUG] calloc not zeroed (%p[%u]=%u, req=%u usable=%u)\n",
                                   p, (unsigned)k, ((uint8_t *)p)[k], (unsigned)requested, (unsigned)usable);
                            die_now();
                        }
                    }
                    ptrs[slot] = p;
                }
            }
            check_invariants_or_die("calloc");
        } else {
            /* free */
            if (ptrs[slot] != NULL) {
                void *p = ptrs[slot];
                if (!ptr_in_heap(p)) {
                    printk("[BUG] freeing non-heap ptr %p\n", p);
                    die_now();
                }
                k_heap_free(&my_heap, p);
                ptrs[slot] = NULL;
            }
            check_invariants_or_die("free");
        }
    }

    /* Cleanup */
    for (int j = 0; j < MAX_ALLOCS; j++) {
        if (ptrs[j] != NULL) {
            k_heap_free(&my_heap, ptrs[j]);
            ptrs[j] = NULL;
        }
    }
    check_invariants_or_die("cleanup");
}

int main(void)
{
    test_once();

    /* Mark completion for runners that look for it. */
    printk("[TEST_CASE_COMPLETED]\n");

    k_msleep(1);
    BREAKPOINT();
    return 0;
}