/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: kernel_heap_fuzz
 * 生成时间: 2025-12-29 18:41:09
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * API类别: kernel
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

// LLM 可在此添加辅助函数（不得使用 FR_ 前缀）
#include <zephyr/kernel.h>
#include <string.h>
#define MAX_ALLOCS 8
static void *alloc_ptrs[MAX_ALLOCS] = {NULL};
static FR_Reader fr_ctx;
static bool fr_started = false;
static bool fr_exhausted = false;
static void perform_cleanup(void) {
    for (int i = 0; i < MAX_ALLOCS; i++) {
        if (alloc_ptrs[i] != NULL) {
            k_free(alloc_ptrs[i]);
            alloc_ptrs[i] = NULL;
        }
    }
}

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        if (fr_exhausted) return;
        if (!fr_started) {
            fr_ctx = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
            fr_started = true;
        }
        if (FR_remaining(&fr_ctx) < 12) {
            perform_cleanup();
            fr_exhausted = true;
            return;
        }
        uint8_t action = FR_next_u8(&fr_ctx) % 4;
        uint8_t index = FR_next_u8(&fr_ctx) % MAX_ALLOCS;
        if (action == 0) {
            size_t sz = (size_t)FR_next_range(&fr_ctx, 0, 256);
            if (alloc_ptrs[index]) {
                k_free(alloc_ptrs[index]);
            }
            alloc_ptrs[index] = k_malloc(sz);
            if (alloc_ptrs[index] && sz > 0) {
                memset(alloc_ptrs[index], 0xAA, sz);
            }
        } else if (action == 1) {
            size_t n = (size_t)FR_next_range(&fr_ctx, 1, 10);
            size_t s = (size_t)FR_next_range(&fr_ctx, 1, 20);
            if (alloc_ptrs[index]) {
                k_free(alloc_ptrs[index]);
            }
            alloc_ptrs[index] = k_calloc(n, s);
            if (alloc_ptrs[index]) {
                uint8_t *p = (uint8_t *)alloc_ptrs[index];
                __ASSERT_NO_MSG(p[0] == 0);
                __ASSERT_NO_MSG(p[n * s - 1] == 0);
            }
        } else if (action == 2) {
            k_free(alloc_ptrs[index]);
            alloc_ptrs[index] = NULL;
        } else {
            size_t sz = (size_t)FR_next_range(&fr_ctx, 0, 128);
            void *tmp = k_malloc(sz);
            if (tmp) {
                k_free(tmp);
            }
        }
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
