/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: atomic_and_condvar_fuzz
 * 生成时间: 2025-12-30 05:01:05
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * API类别: Kernel/Atomics
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
static atomic_t global_atomic = ATOMIC_INIT(0);
static atomic_ptr_t global_atomic_ptr = ATOMIC_PTR_INIT(NULL);
static struct k_condvar global_condvar;
static uint8_t dummy_buffer[256];

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        FR_Reader reader = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

        /* Initialize condition variable */
        int cond_ret = k_condvar_init(&global_condvar);
        __ASSERT(cond_ret == 0, "k_condvar_init failed");

        for (int i = 0; i < 100 && FR_remaining(&reader) > 0; i++) {
            /* 1. Fuzz atomic_t operations */
            atomic_val_t initial_val = (atomic_val_t)FR_next_range(&reader, 0, 0xFFFFFFFF);
            atomic_set(&global_atomic, initial_val);

            /* Clear a specific bit */
            int bit_to_clear = (int)FR_next_range(&reader, 0, 31);
            atomic_clear_bit(&global_atomic, bit_to_clear);

            /* Verify bit is cleared */
            atomic_val_t after_bit_clear = atomic_get(&global_atomic);
            __ASSERT((after_bit_clear & (1 << bit_to_clear)) == 0, "Bit was not cleared");

            /* Clear the whole atomic variable */
            atomic_val_t pre_clear_val = atomic_clear(&global_atomic);
            __ASSERT(pre_clear_val == after_bit_clear, "atomic_clear returned wrong value");
            __ASSERT(atomic_get(&global_atomic) == 0, "atomic_clear did not zero the value");

            /* 2. Fuzz atomic_ptr_t operations */
            uint32_t offset = FR_next_range(&reader, 0, sizeof(dummy_buffer) - 1);
            void *ptr_to_set = &dummy_buffer[offset];
            atomic_ptr_set(&global_atomic_ptr, ptr_to_set);

            /* Clear the atomic pointer */
            atomic_ptr_val_t pre_ptr_clear = atomic_ptr_clear(&global_atomic_ptr);
            __ASSERT(pre_ptr_clear == ptr_to_set, "atomic_ptr_clear returned wrong pointer");
            __ASSERT(atomic_ptr_get(&global_atomic_ptr) == NULL, "atomic_ptr_clear did not nullify");

            /* 3. Re-init condvar to ensure idempotency/stability in loop */
            cond_ret = k_condvar_init(&global_condvar);
            __ASSERT(cond_ret == 0, "Repeated k_condvar_init failed");
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
