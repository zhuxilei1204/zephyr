/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: zephyr_mem_domain_management
 * 目标RTOS: Zephyr
 * API类别: Memory Management
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/app_memory/mem_domain.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>

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

#ifdef CONFIG_USERSPACE
static uint8_t f_b0[512] __aligned(512);
static uint8_t f_b1[512] __aligned(512);
static uint8_t f_b2[512] __aligned(512);
static uint8_t f_b3[512] __aligned(512);

K_MEM_PARTITION_DEFINE(f_p0, f_b0, 512, K_MEM_PARTITION_P_RW_U_RW);
K_MEM_PARTITION_DEFINE(f_p1, f_b1, 512, K_MEM_PARTITION_P_RW_U_RW);
K_MEM_PARTITION_DEFINE(f_p2, f_b2, 512, K_MEM_PARTITION_P_RW_U_RW);
K_MEM_PARTITION_DEFINE(f_p3, f_b3, 512, K_MEM_PARTITION_P_RW_U_RW);

static struct k_mem_partition *f_p_list[] = {&f_p0, &f_p1, &f_p2, &f_p3};
static struct k_mem_domain f_dom;
static bool f_init_done = false;
#endif

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

#ifdef CONFIG_USERSPACE
    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        if (!f_init_done) {
            /* Initialize domain with a fuzzed number of initial partitions (0-4) */
            uint8_t init_cnt = (uint8_t)FR_next_range(&fr, 0, 4);
            int ret = k_mem_domain_init(&f_dom, init_cnt, f_p_list);
            if (ret == 0) {
                f_init_done = true;
            }
        }

        if (f_init_done && FR_remaining(&fr) >= 8) {
            uint32_t action = FR_next_range(&fr, 0, 1);
            uint32_t p_idx = FR_next_range(&fr, 0, 3);
            struct k_mem_partition *target_p = f_p_list[p_idx];

            if (action == 0) {
                /* Try adding a partition to the domain */
                (void)k_mem_domain_add_partition(&f_dom, target_p);
            } else {
                /* Try removing a partition from the domain */
                (void)k_mem_domain_remove_partition(&f_dom, target_p);
            }
        }
    }
#else
    printk("Userspace not enabled, skipping memory domain tests.\n");
#endif

    printk("[TEST_CASE_COMPLETED]\n");
}

int main(void)
{
    test_once();

    /* Give UART/QEMU a brief chance to flush the completion marker. */
    k_msleep(1);

    BREAKPOINT();

    return 0;
}
