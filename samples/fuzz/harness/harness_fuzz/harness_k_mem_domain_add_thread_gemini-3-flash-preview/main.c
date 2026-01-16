/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: mem_domain_lifecycle_fuzz
 * 生成时间: 2026-01-03 21:47:00
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * API类别: memory_management
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/app_memory/mem_domain.h>
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

#ifdef CONFIG_USERSPACE
static struct k_mem_domain domains[2];
static struct k_mem_partition partitions[2];
static uint8_t part_bufs[2][64] __aligned(4096);
static struct k_thread fuzz_threads[2];
K_THREAD_STACK_ARRAY_DEFINE(fuzz_stacks, 2, 1024);
static bool thread_active[2] = {false, false};

void fuzz_thread_entry(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    while (1) { k_yield(); }
}
#endif

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

#ifdef CONFIG_USERSPACE
    for (unsigned int i = 0; i < iterations; ++i) {
        FR_Reader reader = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

        for (int k = 0; k < 2; k++) {
            partitions[k].start = (uintptr_t)part_bufs[k];
            partitions[k].size = sizeof(part_bufs[k]);
            partitions[k].attr = K_MEM_PARTITION_P_RW_U_RW;
        }

        for (int j = 0; j < 32 && FR_remaining(&reader) > 0; j++) {
            uint32_t action = FR_next_range(&reader, 0, 3);
            uint32_t d_idx = FR_next_range(&reader, 0, 1);
            uint32_t t_idx = FR_next_range(&reader, 0, 1);
            uint32_t p_idx = FR_next_range(&reader, 0, 1);

            if (action == 0) {
                struct k_mem_partition *init_parts[1] = { &partitions[p_idx] };
                k_mem_domain_init(&domains[d_idx], 1, init_parts);
            } else if (action == 1) {
                k_mem_domain_add_partition(&domains[d_idx], &partitions[p_idx]);
            } else if (action == 2) {
                if (thread_active[t_idx]) {
                    k_mem_domain_add_thread(&domains[d_idx], &fuzz_threads[t_idx]);
                }
            } else if (action == 3) {
                if (!thread_active[t_idx]) {
                    k_thread_create(&fuzz_threads[t_idx], fuzz_stacks[t_idx], K_THREAD_STACK_SIZEOF(fuzz_stacks[t_idx]),
                                    fuzz_thread_entry, NULL, NULL, NULL,
                                    K_LOWEST_APPLICATION_THREAD_PRIO, K_USER | K_INHERIT_PERMS, K_NO_WAIT);
                    thread_active[t_idx] = true;
                }
            }
        }
    }
#else
    ARG_UNUSED(iterations);
#endif

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
