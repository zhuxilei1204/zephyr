/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: mem_domain_fuzz_test
 * 生成时间: 2026-01-02 22:47:37
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
    __asm volatile("nop");
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

#ifdef CONFIG_USERSPACE
static struct k_mem_domain fuzz_domains[2];
static struct k_mem_partition fuzz_partitions[4];
static uint8_t fuzz_buffers[4][1024] __aligned(4096);

static void init_partitions(void) {
    for (int i = 0; i < 4; i++) {
        fuzz_partitions[i].start = (uintptr_t)fuzz_buffers[i];
        fuzz_partitions[i].size = sizeof(fuzz_buffers[i]);
        fuzz_partitions[i].attr = K_MEM_PARTITION_P_RW_U_RW;
    }
}
#endif

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
#ifdef CONFIG_USERSPACE
        FR_Reader reader = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
        init_partitions();

        for (int j = 0; j < 100 && FR_remaining(&reader) > 0; j++) {
            uint32_t action = FR_next_range(&reader, 0, 2);
            uint32_t domain_idx = FR_next_range(&reader, 0, 1);

            if (action == 0) {
                /* k_mem_domain_init */
                uint8_t num_parts = (uint8_t)FR_next_range(&reader, 0, 4);
                struct k_mem_partition *parts_ptr[4];
                for (uint8_t p = 0; p < num_parts; p++) {
                    uint32_t p_idx = FR_next_range(&reader, 0, 3);
                    parts_ptr[p] = &fuzz_partitions[p_idx];
                }
                k_mem_domain_init(&fuzz_domains[domain_idx], num_parts, (num_parts > 0) ? parts_ptr : NULL);
            } else if (action == 1) {
                /* k_mem_domain_add_partition */
                uint32_t p_idx = FR_next_range(&reader, 0, 3);
                k_mem_domain_add_partition(&fuzz_domains[domain_idx], &fuzz_partitions[p_idx]);
            } else if (action == 2) {
                /* k_mem_domain_add_thread */
                k_mem_domain_add_thread(&fuzz_domains[domain_idx], k_current_get());
            }
        }
#else
        printk("CONFIG_USERSPACE not enabled, skipping memory domain fuzzing.\n");
#endif
        printk("[TEST_CASE_COMPLETED]\n");
    }

    printk("[TEST_CASE_COMPLETED]\n");
}

int main(void)
{
    test_once();

    /* Use busy wait instead of msleep to avoid yielding in single-threaded environment */
    k_busy_wait(1000);

    BREAKPOINT();

    return 0;
}
