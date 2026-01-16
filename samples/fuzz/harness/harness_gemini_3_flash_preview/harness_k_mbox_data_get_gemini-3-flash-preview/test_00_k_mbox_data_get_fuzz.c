/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: k_mbox_data_get_fuzz
 * 目标RTOS: Zephyr
 * API类别: Kernel Mailbox
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/devicetree.h>
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

K_MBOX_DEFINE(fuzz_mbox);
static uint8_t tx_buf[128];
static uint8_t rx_buf[128];

/* Fix: DT macros require correct argument counts for VARGS variants */
#ifdef DT_N_S_memory_20000000_P_compatible_FOREACH_PROP_ELEM_VARGS
#define DUMMY_PROP_ELEM(node_id, prop, idx, ...) idx,
static int dummy_mem_props[] __attribute__((unused)) = { DT_N_S_memory_20000000_P_compatible_FOREACH_PROP_ELEM_VARGS(DUMMY_PROP_ELEM) };
#endif

#ifdef DT_N_S_gpio_keys_S_button_1_FOREACH_CHILD_VARGS
#define DUMMY_CHILD_ELEM(node_id, ...) 1,
static int dummy_btn_children[] __attribute__((unused)) = { DT_N_S_gpio_keys_S_button_1_FOREACH_CHILD_VARGS(DUMMY_CHILD_ELEM) };
#endif

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    /* Consume baseline bytes */
    unsigned char baseline[16];
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    uint32_t iterations = FR_next_range(&fr, 1, 10);

    for (uint32_t i = 0; i < iterations; ++i) {
        if (FR_remaining(&fr) < 16) {
            break;
        }

        uint32_t size = FR_next_range(&fr, 0, 128);
        (void)FR_next_bytes(&fr, tx_buf, size);

        /* Use static to ensure persistence in single-threaded async context */
        static struct k_mbox_msg tx_msg;
        static struct k_sem sem;

        memset(&tx_msg, 0, sizeof(tx_msg));
        tx_msg.size = size;
        tx_msg.info = (uint16_t)FR_next_range(&fr, 0, 0xFFFF);
        tx_msg.tx_data = tx_buf;
        tx_msg.tx_target_thread = K_ANY;

        k_sem_init(&sem, 0, 1);

        /* Async put to avoid blocking the single thread */
        k_mbox_async_put(&fuzz_mbox, &tx_msg, &sem);

        struct k_mbox_msg rx_msg;
        memset(&rx_msg, 0, sizeof(rx_msg));
        rx_msg.size = size;
        rx_msg.rx_source_thread = K_ANY;

        /* Attempt to retrieve the message header */
        int ret = k_mbox_get(&fuzz_mbox, &rx_msg, NULL, K_NO_WAIT);
        if (ret == 0) {
            uint32_t choice = FR_next_range(&fr, 0, 1);
            void *target_ptr = (choice == 0) ? rx_buf : NULL;

            /* Target API: k_mbox_data_get */
            k_mbox_data_get(&rx_msg, target_ptr);
        }
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
