/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: fuzz_thread_stack_and_queue_peek
 * 生成时间: 2026-01-04 07:19:51
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
    __asm volatile("nop");
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

struct fuzz_item_t {
    void *fifo_reserved;
    uint32_t data;
};

K_THREAD_STACK_DEFINE(static_fuzz_stack, 2048);
K_THREAD_STACK_DECLARE(static_fuzz_stack, 2048);

static void dummy_stack_user(k_thread_stack_t *stack, size_t size) {
    if (stack == NULL) return;
    uint8_t *ptr = (uint8_t *)stack;
    if (size > 0) {
        ptr[0] = 0xAA;
    }
}

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 5);

    for (unsigned int i = 0; i < iterations; ++i) {
        struct k_queue fuzz_q;
        k_queue_init(&fuzz_q);

        #define MAX_DYN_STACKS 2
        k_thread_stack_t *dyn_stacks[MAX_DYN_STACKS] = {NULL, NULL};
        struct fuzz_item_t q_items[2];
        bool item_in_queue[2] = {false, false};

        for (int k = 0; k < 2; k++) {
            q_items[k].data = (uint32_t)k;
        }

        size_t static_sz = K_THREAD_STACK_SIZEOF(static_fuzz_stack);
        if (static_sz < 2048) return;

        for (int j = 0; j < 20 && FR_remaining(&fr) > 0; ++j) {
            uint32_t action = FR_next_range(&fr, 0, 4);

            switch (action) {
            case 0: { /* k_thread_stack_alloc */
                uint32_t idx = FR_next_range(&fr, 0, MAX_DYN_STACKS - 1);
                if (dyn_stacks[idx] == NULL) {
                    size_t sz = (size_t)FR_next_range(&fr, 256, 1024);
                    int flags = (FR_next_range(&fr, 0, 1) == 1) ? K_USER : 0;
                    dyn_stacks[idx] = k_thread_stack_alloc(sz, flags);
                    if (dyn_stacks[idx]) {
                        dummy_stack_user(dyn_stacks[idx], sz);
                    }
                }
                break;
            }
            case 1: { /* k_thread_stack_free */
                uint32_t idx = FR_next_range(&fr, 0, MAX_DYN_STACKS - 1);
                if (dyn_stacks[idx] != NULL) {
                    int ret = k_thread_stack_free(dyn_stacks[idx]);
                    if (ret == 0) {
                        dyn_stacks[idx] = NULL;
                    }
                }
                break;
            }
            case 2: { /* k_queue_peek_head */
                void *head = k_queue_peek_head(&fuzz_q);
                if (head != NULL) {
                    struct fuzz_item_t *item = (struct fuzz_item_t *)head;
                    if (item->data >= 2) return;
                }
                break;
            }
            case 3: { /* k_queue_append */
                uint32_t idx = FR_next_range(&fr, 0, 1);
                if (!item_in_queue[idx]) {
                    k_queue_append(&fuzz_q, &q_items[idx]);
                    item_in_queue[idx] = true;
                }
                break;
            }
            case 4: { /* k_queue_get (Non-blocking) */
                void *item = k_queue_get(&fuzz_q, K_NO_WAIT);
                if (item != NULL) {
                    for (int k = 0; k < 2; k++) {
                        if (item == &q_items[k]) {
                            item_in_queue[k] = false;
                        }
                    }
                }
                break;
            }
            }
        }

        for (int k = 0; k < MAX_DYN_STACKS; k++) {
            if (dyn_stacks[k] != NULL) {
                k_thread_stack_free(dyn_stacks[k]);
                dyn_stacks[k] = NULL;
            }
        }

        while (k_queue_get(&fuzz_q, K_NO_WAIT)) { }
    }

    printk("[TEST_CASE_COMPLETED]\n");
}

int main(void)
{
    test_once();
    BREAKPOINT();
    return 0;
}
