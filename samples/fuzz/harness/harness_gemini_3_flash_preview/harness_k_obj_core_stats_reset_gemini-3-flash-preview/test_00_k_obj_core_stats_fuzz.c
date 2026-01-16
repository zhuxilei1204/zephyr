/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: k_obj_core_stats_fuzz
 * 目标RTOS: Zephyr
 * API类别: kernel
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel/obj_core.h>
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
    return (uint32_t)((hi << 16) | lo);
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

static struct k_sem fuzzer_sem;
static bool sem_init_done = false;
static uint8_t fuzzer_stats_buf[128];

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    if (!sem_init_done) {
        /* Initialize a standard kernel object */
        k_sem_init(&fuzzer_sem, 0, 100);
        sem_init_done = true;
    }

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        if (FR_remaining(&fr) == 0) {
            break;
        }

        uint32_t cmd = FR_next_range(&fr, 0, 3);

        /* Object Core Stats APIs require specific Kconfig options to be enabled. 
         * We wrap them in #if defined checks to ensure compilation even if the 
         * target board configuration has them disabled. 
         */
#if defined(CONFIG_OBJ_CORE_SEM) && defined(CONFIG_OBJ_CORE_STATS)
        struct k_obj_core *core = K_OBJ_CORE(&fuzzer_sem);

        if (cmd == 0) {
            /* Fuzz k_obj_core_stats_query with various buffer lengths */
            size_t query_len = (size_t)FR_next_range(&fr, 0, sizeof(fuzzer_stats_buf));
            int ret = k_obj_core_stats_query(core, fuzzer_stats_buf, query_len);
            (void)ret;
        } else if (cmd == 1) {
            /* Fuzz k_obj_core_stats_reset */
            int ret = k_obj_core_stats_reset(core);
            (void)ret;
        } else if (cmd == 2) {
            /* Access the descriptor associated with the object core */
            /* k_obj_core_stats_desc is a static inline in zephyr/kernel/obj_core.h */
            const void *desc = (const void *)k_obj_core_stats_desc(core);
            (void)desc;
        } else {
            /* Generate some activity to populate statistics */
            k_sem_give(&fuzzer_sem);
            (void)k_sem_take(&fuzzer_sem, K_NO_WAIT);
        }
#else
        /* Fallback: just perform basic semaphore operations if stats are unavailable */
        k_sem_give(&fuzzer_sem);
        (void)k_sem_take(&fuzzer_sem, K_NO_WAIT);
        (void)cmd;
#endif
    }

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
