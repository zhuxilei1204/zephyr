/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: k_event_lifecycle_fuzz
 * 生成时间: 2026-01-05 15:00:04
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * API类别: kernel_event
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
static struct k_event fuzzer_event;

/* Helper to safely exercise DT macros if they exist on the target */
#define DT_SEED_VAL(node_id) DT_REG_ADDR(node_id),

static uint32_t get_dt_seed(int index) {
    static const uint32_t dt_seeds[] = {
        0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80
    };
    return dt_seeds[index % 8];
}

static void validate_event_state(struct k_event *ev, uint32_t unexpected_mask) {
    /* Use k_event_wait with K_NO_WAIT to poll current state without blocking */
    uint32_t current = k_event_wait(ev, 0xFFFFFFFF, false, K_NO_WAIT);
    if ((current & unexpected_mask) != 0) {
        __ASSERT(false, "Event state contains cleared bits: 0x%08x", (current & unexpected_mask));
    }
}

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        FR_Reader reader = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

        /* Initialize the event object */
        k_event_init(&fuzzer_event);

        /* Bounded loop to process fuzzing iterations */
        for (int i = 0; i < 32 && FR_remaining(&reader) > 4; ++i) {
            uint32_t action = FR_next_range(&reader, 0, 3);
            uint32_t mask = FR_next_range(&reader, 0, 0xFFFFFFFF);
    
            /* Mix in some DT-derived seeds to exercise specific bit patterns */
            if (i % 4 == 0) {
                mask |= get_dt_seed(i);
            }

            switch (action) {
            case 0: /* k_event_post: ORs bits */
                k_event_post(&fuzzer_event, mask);
                break;
            case 1: /* k_event_set: Overwrites bits */
                k_event_set(&fuzzer_event, mask);
                break;
            case 2: /* k_event_clear: Removes bits */
                {
                    uint32_t prev = k_event_clear(&fuzzer_event, mask);
                    /* Verify that bits in 'mask' are no longer set */
                    validate_event_state(&fuzzer_event, mask);
                    (void)prev;
                }
                break;
            case 3: /* k_event_wait: Non-blocking check */
                {
                    bool wait_all = (mask & 0x1);
                    k_event_wait(&fuzzer_event, mask, wait_all, K_NO_WAIT);
                }
                break;
            default:
                break;
            }
        }

        /* Final cleanup/reset for next iteration if applicable */
        k_event_set(&fuzzer_event, 0);
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
