/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: zephyr_poll_api_fuzz
 * 生成时间: 2025-12-30 02:35:49
 * 目标RTOS: Zephyr
 * 项目路径: /home/zwz/zephyr/samples/fuzz
 * API类别: kernel
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

static struct k_poll_signal f_sig;
static struct k_sem f_sem;
static struct k_msgq f_msgq;
static char __aligned(4) f_msgq_buf[8];
static struct k_poll_event f_events[1];
static bool f_init_done = false;

static void test_once(void)
{
    if (!f_init_done) {
        k_poll_signal_init(&f_sig);
        k_sem_init(&f_sem, 0, 1);
        k_msgq_init(&f_msgq, f_msgq_buf, 1, 8);
        f_init_done = true;
    }

    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
    unsigned int iterations = (unsigned int)FR_next_range(&fr, 1, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        if (FR_remaining(&fr) < 12) {
            break;
        }

        uint32_t type_choice = FR_next_range(&fr, 0, 3);
        uint32_t trigger = FR_next_range(&fr, 0, 1);
        void *obj = &f_sig;
        uint32_t type = K_POLL_TYPE_IGNORE;

        switch (type_choice) {
            case 1:
                type = K_POLL_TYPE_SIGNAL;
                obj = &f_sig;
                break;
            case 2:
                type = K_POLL_TYPE_SEM_AVAILABLE;
                obj = &f_sem;
                break;
            case 3:
                type = K_POLL_TYPE_MSGQ_DATA_AVAILABLE;
                obj = &f_msgq;
                break;
            default:
                type = K_POLL_TYPE_IGNORE;
                obj = &f_sig;
                break;
        }

        k_poll_event_init(&f_events[0], type, K_POLL_MODE_NOTIFY_ONLY, obj);

        if (trigger) {
            if (type == K_POLL_TYPE_SIGNAL) {
                k_poll_signal_raise(&f_sig, 0xABC);
            } else if (type == K_POLL_TYPE_SEM_AVAILABLE) {
                k_sem_give(&f_sem);
            } else if (type == K_POLL_TYPE_MSGQ_DATA_AVAILABLE) {
                char dummy = 0;
                k_msgq_put(&f_msgq, &dummy, K_NO_WAIT);
            }
        }

        (void)k_poll(f_events, 1, K_NO_WAIT);

        // Cleanup state to avoid accumulation across iterations
        k_poll_signal_reset(&f_sig);
        k_sem_take(&f_sem, K_NO_WAIT);
        char d;
        k_msgq_get(&f_msgq, &d, K_NO_WAIT);
    }

    printk("[TEST_CASE_COMPLETED]\n");
}

int main(void)
{
    test_once();
    k_msleep(10);
    BREAKPOINT();
    return 0;
}
