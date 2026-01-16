/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: k_obj_core_lifecycle_fuzz
 * 目标RTOS: Zephyr
 * API类别: kernel_object
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel/obj_core.h>
#include <zephyr/sys/slist.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

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

#ifdef CONFIG_OBJ_CORE
struct my_fuzz_obj {
    struct k_obj_core obj_core;
    uint32_t val;
};

#define MAX_TEST_OBJS 4
static struct my_fuzz_obj test_objs[MAX_TEST_OBJS];
static struct k_obj_type test_type = {
    .list = SYS_SLIST_STATIC_INIT(&test_type.list),
    .id = 0x1234,
    .obj_size = sizeof(struct my_fuzz_obj)
};
static bool is_linked[MAX_TEST_OBJS];
static bool is_inited[MAX_TEST_OBJS];
#endif

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

#ifdef CONFIG_OBJ_CORE
    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        FR_Reader reader = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

        for (int j = 0; j < 64 && FR_remaining(&reader) > 0; j++) {
            uint32_t obj_idx = FR_next_range(&reader, 0, MAX_TEST_OBJS - 1);
            uint32_t action = FR_next_range(&reader, 0, 2);
            struct k_obj_core *core_ptr = K_OBJ_CORE(&test_objs[obj_idx]);

            switch (action) {
                case 0: /* k_obj_core_init_and_link */
                    if (!is_linked[obj_idx]) {
                        k_obj_core_init_and_link(core_ptr, &test_type);
                        is_linked[obj_idx] = true;
                        is_inited[obj_idx] = true;
                    }
                    break;
                case 1: /* k_obj_core_link */
                    if (is_inited[obj_idx] && !is_linked[obj_idx]) {
                        k_obj_core_link(core_ptr, &test_type);
                        is_linked[obj_idx] = true;
                    }
                    break;
                case 2: /* k_obj_core_unlink */
                    if (is_linked[obj_idx]) {
                        k_obj_core_unlink(core_ptr);
                        is_linked[obj_idx] = false;
                    }
                    break;
            }
        }

        /* Cleanup to leave system in clean state */
        for (int k = 0; k < MAX_TEST_OBJS; k++) {
            if (is_linked[k]) {
                k_obj_core_unlink(K_OBJ_CORE(&test_objs[k]));
                is_linked[k] = false;
            }
            is_inited[k] = false;
        }
    }
#else
    printk("CONFIG_OBJ_CORE not enabled, skipping logic\n");
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
