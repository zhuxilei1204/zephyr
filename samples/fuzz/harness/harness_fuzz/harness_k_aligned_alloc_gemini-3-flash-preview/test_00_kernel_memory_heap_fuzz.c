/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: kernel_memory_heap_fuzz
 * 生成时间: 2025-12-30 09:55:59
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * API类别: kernel_memory
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
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
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/util.h>

#define TEST_HEAP_SIZE 4096
static char test_heap_mem[TEST_HEAP_SIZE] __aligned(64);
static struct k_heap test_k_heap;
static struct sys_heap test_sys_heap;
static bool heaps_initialized = false;

static void setup_heaps(void) {
    if (!heaps_initialized) {
        /* Initialize k_heap using the first half of the buffer */
        k_heap_init(&test_k_heap, test_heap_mem, TEST_HEAP_SIZE / 2);
        /* Initialize sys_heap using the second half of the buffer */
        sys_heap_init(&test_sys_heap, test_heap_mem + (TEST_HEAP_SIZE / 2), TEST_HEAP_SIZE / 2);
        heaps_initialized = true;
    }
}

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        FR_Reader reader = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
        setup_heaps();

        for (int i = 0; i < 64 && FR_remaining(&reader) > 0; ++i) {
            uint32_t api_selector = FR_next_range(&reader, 0, 2);
            uint32_t size = FR_next_range(&reader, 0, 512);
            /* Alignment must be a power of 2. We test 1, 2, 4, 8, 16, 32, 64. */
            uint32_t align_shift = FR_next_range(&reader, 0, 6);
            size_t align = (size_t)1 << align_shift;

            if (api_selector == 0) {
                /* Test k_aligned_alloc and k_free (System Heap) */
                /* Note: k_aligned_alloc requires CONFIG_HEAP_MEM_POOL_SIZE > 0 */
                void *ptr = k_aligned_alloc(align, size);
                if (ptr) {
                    __ASSERT(((uintptr_t)ptr % align) == 0, "System heap alignment mismatch");
                    k_free(ptr);
                }
            } else if (api_selector == 1) {
                /* Test k_heap_aligned_alloc and k_heap_free */
                void *ptr = k_heap_aligned_alloc(&test_k_heap, align, size, K_NO_WAIT);
                if (ptr) {
                    __ASSERT(((uintptr_t)ptr % align) == 0, "k_heap alignment mismatch");
                    k_heap_free(&test_k_heap, ptr);
                }
            } else {
                /* Test sys_heap_aligned_alloc and sys_heap_free */
                void *ptr = sys_heap_aligned_alloc(&test_sys_heap, align, size);
                if (ptr) {
                    __ASSERT(((uintptr_t)ptr % align) == 0, "sys_heap alignment mismatch");
                    sys_heap_free(&test_sys_heap, ptr);
                }
            }
        }

        /* Final check: ensure NULL pointers are handled safely by free functions */
        k_free(NULL);
        k_heap_free(&test_k_heap, NULL);
        sys_heap_free(&test_sys_heap, NULL);
    }

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
