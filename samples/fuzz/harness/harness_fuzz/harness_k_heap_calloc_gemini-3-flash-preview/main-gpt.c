/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: k_heap_fuzz_test
 * 生成时间: 2026-01-03 19:37:34
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * API类别: kernel_heap
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_heap.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

// LibAFL integration: deterministic global input buffer
#define MAX_FUZZ_INPUT_SIZE 1024
__attribute__((used, visibility("default"))) unsigned char FUZZ_INPUT[MAX_FUZZ_INPUT_SIZE] = {
    0x46, 0x55, 0x5a, 0x5a, 0x01, 0x23, 0x45, 0x67,
    0x89, 0xab, 0xcd, 0xef, 0x11, 0x22, 0x33, 0x44,
    0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc,
    0xdd, 0xee, 0xff, 0x00, 0x13, 0x37, 0x42, 0x24,
    0x5a, 0xa5, 0xc3, 0x3c, 0xde, 0xed, 0xbe, 0xef,
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0x0f,
    0x1f, 0x2f, 0x3f, 0x4f, 0x5f, 0x6f, 0x7f, 0x8f
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

static inline const char* FR_next_string(FR_Reader* r, size_t max_len)
{
    /* Returns a pointer to a static buffer (overwritten on each call). */
    static char s_buf[64];
    size_t cap = sizeof(s_buf) - 1;
    size_t want = max_len;
    if (want == 0) {
        want = 1;
    }
    if (want > cap) {
        want = cap;
    }

    size_t rem = FR_remaining(r);
    if (want > rem) {
        want = rem;
    }

    for (size_t i = 0; i < want; ++i) {
        /* Map bytes into a safe printable subset: [a-z0-9_] */
        uint8_t b = FR_next_u8(r);
        uint8_t sel = (uint8_t)(b % 37u);
        if (sel < 26u) {
            s_buf[i] = (char)('a' + sel);
        } else if (sel < 36u) {
            s_buf[i] = (char)('0' + (sel - 26u));
        } else {
            s_buf[i] = '_';
        }
    }

    s_buf[want] = '\0';
    return s_buf;
}

// LLM 可在此添加辅助函数（不得使用 FR_ 前缀）
#define MAX_ALLOCS 8
#define HEAP_SIZE 2048

#define PAD_WORDS 1024
#define PAD_PRE_PATTERN  0xA5A5A5A5u
#define PAD_POST_PATTERN 0x5A5A5A5Au

struct safe_heap_layout {
    volatile uint32_t padding_pre[PAD_WORDS];
    uint8_t heap_mem[HEAP_SIZE];
    volatile uint32_t padding_post[PAD_WORDS];
} __attribute__((aligned(8)));

static struct safe_heap_layout my_layout;
static struct k_heap my_heap;
static void *ptrs[MAX_ALLOCS];

static inline void die_now(void)
{
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
     * Per-execution fuzz harness:
     * - Always re-init heap to avoid cross-input state.
     * - Validate heap integrity and guard bands after each op.
     * - Limit reader size to reduce dependence on stale bytes if fuzzer input is short.
     */
    padding_init();
    k_heap_init(&my_heap, my_layout.heap_mem, HEAP_SIZE);
    for (int i = 0; i < MAX_ALLOCS; i++) {
        ptrs[i] = NULL;
    }
    check_invariants_or_die("init");

    FR_Reader reader = FR_init(FUZZ_INPUT, 128);
    unsigned char baseline[8] = {0};
    (void)FR_next_bytes(&reader, baseline, sizeof(baseline));

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

    for (int i = 0; i < MAX_ALLOCS; i++) {
        if (ptrs[i] != NULL) {
            k_heap_free(&my_heap, ptrs[i]);
            ptrs[i] = NULL;
        }
    }
    check_invariants_or_die("cleanup");

    printk("[TEST_CASE_COMPLETED]\n");
}

int main(void)
{
    test_once();

    /* Give UART/QEMU a brief chance to flush the completion marker. */
    k_msleep(1);

    // 保持停住，runner 看到标记后会终止 QEMU/west
    BREAKPOINT();

    return 0;
}
