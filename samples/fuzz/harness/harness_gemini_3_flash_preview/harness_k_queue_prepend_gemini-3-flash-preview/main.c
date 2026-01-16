/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: k_queue_prepend_fuzz_test
 * 生成时间: 2026-01-04 21:13:08
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

struct fuzzer_item {
    void *fifo_reserved;
    uint32_t id;
};

#define MAX_ITEMS 8
static struct fuzzer_item items[MAX_ITEMS];
static bool item_in_use[MAX_ITEMS];
static struct k_spinlock state_lock;
static struct k_queue fuzzer_q;

static void reset_state(void) {
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    for (int i = 0; i < MAX_ITEMS; i++) {
        items[i].id = i;
        item_in_use[i] = false;
    }
    k_spin_unlock(&state_lock, key);
}

static void test_once(void)
{
    /* Ensure queue is initialized even if iterations is 0 to avoid hang in cleanup */
    k_queue_init(&fuzzer_q);
    reset_state();

    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        k_queue_init(&fuzzer_q);
        reset_state();

        FR_Reader reader = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

        for (int j = 0; j < 100; j++) {
            if (FR_remaining(&reader) < 2) break;

            uint32_t action = FR_next_range(&reader, 0, 3);
            uint32_t item_idx = FR_next_range(&reader, 0, MAX_ITEMS - 1);

            if (action == 0) {
                /* Action: Prepend */
                k_spinlock_key_t key = k_spin_lock(&state_lock);
                if (!item_in_use[item_idx]) {
                    item_in_use[item_idx] = true;
                    k_spin_unlock(&state_lock, key);
                    k_queue_prepend(&fuzzer_q, &items[item_idx]);
                } else {
                    k_spin_unlock(&state_lock, key);
                }
            } else if (action == 1) {
                /* Action: Append */
                k_spinlock_key_t key = k_spin_lock(&state_lock);
                if (!item_in_use[item_idx]) {
                    item_in_use[item_idx] = true;
                    k_spin_unlock(&state_lock, key);
                    k_queue_append(&fuzzer_q, &items[item_idx]);
                } else {
                    k_spin_unlock(&state_lock, key);
                }
            } else if (action == 2) {
                /* Action: Get (Non-blocking) */
                struct fuzzer_item *ret = k_queue_get(&fuzzer_q, K_NO_WAIT);
                if (ret != NULL) {
                    k_spinlock_key_t key = k_spin_lock(&state_lock);
                    __ASSERT(ret->id < MAX_ITEMS, "Corrupted item ID retrieved");
                    item_in_use[ret->id] = false;
                    k_spin_unlock(&state_lock, key);
                }
            } else {
                /* Action: Peek and Check Empty */
                (void)k_queue_peek_head(&fuzzer_q);
                (void)k_queue_is_empty(&fuzzer_q);
            }
        }

        /* Cleanup: Drain queue to ensure no dangling pointers */
        while (!k_queue_is_empty(&fuzzer_q)) {
            struct fuzzer_item *ret = k_queue_get(&fuzzer_q, K_NO_WAIT);
            if (ret == NULL) break;
        }

        printk("[TEST_CASE_COMPLETED]\n");
    }

    /* Final drain for safety */
    while (!k_queue_is_empty(&fuzzer_q)) {
        (void)k_queue_get(&fuzzer_q, K_NO_WAIT);
    }

    printk("[TEST_CASE_COMPLETED]\n");
}

int main(void)
{
    test_once();

    /* Use busy wait instead of msleep to avoid potential scheduler issues in harness */
    k_busy_wait(1000);

    BREAKPOINT();

    return 0;
}
