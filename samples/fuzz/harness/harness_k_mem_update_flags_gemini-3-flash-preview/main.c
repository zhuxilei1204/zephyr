/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: k_mem_management_fuzz
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * API类别: Memory Management (Heap)
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

/* Define a heap for fuzzing since mps2/an385 (Cortex-M3) lacks MMU for k_mem_map */
K_HEAP_DEFINE(fuzz_heap, 8192);

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    /* Consume some initial entropy */
    (void)FR_next_u32(&fr);

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 1, 20);

    for (unsigned int i = 0; i < iterations; ++i) {
        if (FR_remaining(&fr) < 8) {
            break;
        }

        /* Fuzz allocation size and alignment */
        size_t alloc_size = (size_t)FR_next_range(&fr, 1, 1024);
        uint8_t align_pow = (uint8_t)FR_next_range(&fr, 0, 8); // 2^0 to 2^8
        size_t alignment = (size_t)(1 << align_pow);

        /* Use k_heap_aligned_alloc as it is a core memory management API */
        void *ptr = k_heap_aligned_alloc(&fuzz_heap, alignment, alloc_size, K_NO_WAIT);

        if (ptr != NULL) {
            /* Perform a fuzzed write to ensure memory is accessible */
            size_t write_off = (size_t)FR_next_range(&fr, 0, alloc_size - 1);
            size_t write_len = (size_t)FR_next_range(&fr, 1, alloc_size - write_off);
            
            unsigned char val = FR_next_u8(&fr);
            memset((uint8_t *)ptr + write_off, val, write_len);

            /* Cleanup */
            k_heap_free(&fuzz_heap, ptr);
        }
    }

    printk("[TEST_CASE_COMPLETED]\n");
}

int main(void)
{
    test_once();

    /* Give UART/QEMU a brief chance to flush the completion marker. */
    k_msleep(1);

    /* Runner will terminate QEMU upon seeing the marker */
    BREAKPOINT();

    return 0;
}
