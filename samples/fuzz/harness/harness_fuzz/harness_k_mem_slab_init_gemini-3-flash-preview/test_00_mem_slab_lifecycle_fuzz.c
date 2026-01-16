/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: mem_slab_lifecycle_fuzz
 * 生成时间: 2025-12-30 14:10:47
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * API类别: Memory Management
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
    return 0;
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

static struct k_mem_slab fuzzer_slab;
#define MAX_BLOCKS 16
#define MAX_BLOCK_SIZE 64
static char slab_buffer[MAX_BLOCKS * MAX_BLOCK_SIZE] __aligned(4);
static void *allocated_ptrs[MAX_BLOCKS];

static void reset_ptrs(void) {
    for (int i = 0; i < MAX_BLOCKS; i++) {
        allocated_ptrs[i] = NULL;
    }
}

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    uint32_t iterations = FR_next_range(&fr, 0, 10);

    for (uint32_t i = 0; i < iterations; ++i) {
        FR_Reader reader = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
        reset_ptrs();

        uint32_t block_size = (uint32_t)FR_next_range(&reader, 8, MAX_BLOCK_SIZE);
        block_size = (block_size + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1);
        uint32_t num_blocks = (uint32_t)FR_next_range(&reader, 1, MAX_BLOCKS);

        int init_res = k_mem_slab_init(&fuzzer_slab, slab_buffer, block_size, num_blocks);
        if (init_res != 0) {
            continue;
        }

        for (int k = 0; k < 32 && FR_remaining(&reader) > 0; k++) {
            uint32_t action = FR_next_range(&reader, 0, 2);
    
            if (action == 0) {
                void *ptr = NULL;
                int alloc_res = k_mem_slab_alloc(&fuzzer_slab, &ptr, K_NO_WAIT);
                if (alloc_res == 0 && ptr != NULL) {
                    bool stored = false;
                    for (int j = 0; j < MAX_BLOCKS; j++) {
                        if (allocated_ptrs[j] == NULL) {
                            allocated_ptrs[j] = ptr;
                            stored = true;
                            break;
                        }
                    }
                    if (!stored) {
                        k_mem_slab_free(&fuzzer_slab, ptr);
                    }
                }
            } else if (action == 1) {
                uint32_t idx = FR_next_range(&reader, 0, MAX_BLOCKS - 1);
                if (allocated_ptrs[idx] != NULL) {
                    k_mem_slab_free(&fuzzer_slab, allocated_ptrs[idx]);
                    allocated_ptrs[idx] = NULL;
                }
            } else {
                /* Action 2: Consume idx but do nothing to avoid potential hang/crash from k_thread_stack_free */
                (void)FR_next_range(&reader, 0, MAX_BLOCKS - 1);
            }
        }

        for (int j = 0; j < MAX_BLOCKS; j++) {
            if (allocated_ptrs[j] != NULL) {
                k_mem_slab_free(&fuzzer_slab, allocated_ptrs[j]);
                allocated_ptrs[j] = NULL;
            }
        }

        printk("[TEST_CASE_COMPLETED]\n");
    }

    printk("[TEST_CASE_COMPLETED]\n");
}

int main(void)
{
    test_once();
    return 0;
}
