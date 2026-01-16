/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: kernel_obj_core_fuzz
 * 生成时间: 2026-01-03 01:21:35
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * API类别: kernel
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel/obj_core.h>
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

static struct k_sem fuzzer_sem;
static bool fuzzer_init_done = false;

static void test_once(void)
{
    if (!fuzzer_init_done) {
        k_sem_init(&fuzzer_sem, 0, 1);
        fuzzer_init_done = true;
    }

    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
    unsigned int iterations = (unsigned int)FR_next_range(&fr, 1, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        if (FR_remaining(&fr) < 4) {
            break;
        }

        uint32_t type_id = FR_next_u32(&fr);

#ifdef CONFIG_OBJ_CORE
        /* API: k_obj_type_find - Test lookup with fuzzed input */
        struct k_obj_type *fuzzed_type = k_obj_type_find(type_id);

        /* API: K_OBJ_TYPE_SEM_ID - Test lookup with a known standard ID */
        struct k_obj_type *sem_type = k_obj_type_find(K_OBJ_TYPE_SEM_ID);

        /* API: K_OBJ_CORE - Extract the core structure from the semaphore */
        /* Note: This macro only works if CONFIG_OBJ_CORE is enabled and the object supports it */
        struct k_obj_core *sem_core = K_OBJ_CORE(&fuzzer_sem);

        if (sem_core != NULL) {
            if (sem_core->type != NULL) {
                /* Basic sanity check: the type ID should be non-zero for valid types */
                if (sem_core->type->id == 0) {
                    // Handle unexpected state or just log
                }
            }
        }

        if (fuzzed_type != NULL) {
            if (fuzzed_type->id != type_id) {
                // Handle unexpected state
            }
        }

        (void)sem_type;
#else
        /* If CONFIG_OBJ_CORE is not enabled, these APIs are not available or the struct member is missing */
        (void)type_id;
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
