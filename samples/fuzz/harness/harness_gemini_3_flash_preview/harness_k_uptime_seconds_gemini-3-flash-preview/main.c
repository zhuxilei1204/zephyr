/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: kernel_uptime_fuzz
 * 生成时间: 2025-12-31 02:41:28
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * API类别: Kernel Uptime
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
static FR_Reader reader;
static bool reader_initialized = false;

static void validate_consistency(void) {
    int64_t ms = k_uptime_get();
    int64_t ticks = k_uptime_ticks();
    uint32_t seconds = k_uptime_seconds();

    /* Basic monotonic checks */
    __ASSERT(ms >= 0, "Uptime MS should be non-negative");
    __ASSERT(ticks >= 0, "Uptime Ticks should be non-negative");

    /* Seconds to MS consistency check */
    __ASSERT(ms >= (int64_t)seconds * 1000, "Uptime seconds/ms mismatch");

    /* Ticks to MS consistency check (approximate due to rounding) */
    int64_t ms_from_ticks = k_ticks_to_ms_floor64(ticks);
    __ASSERT(ms >= ms_from_ticks, "Uptime ticks/ms mismatch");
}

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        if (!reader_initialized) {
            reader = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
            reader_initialized = true;
        }

        if (FR_remaining(&reader) < 12) {
            return;
        }

        /* 1. Validate current uptime consistency */
        validate_consistency();

        /* 2. Fuzz K_SECONDS macro with various inputs */
        uint32_t fuzzed_sec = FR_next_range(&reader, 0, 86400); /* Up to 1 day */
        k_timeout_t timeout = K_SECONDS(fuzzed_sec);
        __ASSERT(timeout.ticks >= 0, "K_SECONDS produced negative ticks");

        /* 3. Fuzz k_uptime_delta with fuzzed reference times */
        int64_t ref_time;
        uint32_t low = FR_next_range(&reader, 0, 0xFFFFFFFF);
        uint32_t high = FR_next_range(&reader, 0, 0xFFFFFFFF);
        ref_time = ((int64_t)high << 32) | (int64_t)low;

        /* Capture current time to verify delta update */
        int64_t pre_delta_ms = k_uptime_get();
        int64_t delta = k_uptime_delta(&ref_time);

        /* k_uptime_delta must update the reference time to the current uptime */
        __ASSERT(ref_time >= pre_delta_ms, "k_uptime_delta did not update reftime correctly");
        (void)delta;

        /* 4. Final consistency check after operations */
        validate_consistency();
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
