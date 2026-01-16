/*
 * Zephyr fuzz single test (mps2/an385)
 * 文件: kernel_memory_and_timer_fuzz
 * 目标RTOS: Zephyr
 * API类别: kernel
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/device.h>
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

static struct k_timer fuzzer_timer;
static uint8_t fuzzer_data[32];
static FR_Reader reader;
static bool reader_initialized = false;

static void init_fuzzer_once(void) {
    if (!reader_initialized) {
        reader = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);
        k_timer_init(&fuzzer_timer, NULL, NULL);
        reader_initialized = true;
    }
}

static void test_once(void)
{
    FR_Reader fr = FR_init(FUZZ_INPUT, MAX_FUZZ_INPUT_SIZE);

    unsigned char baseline[16] = {0};
    (void)FR_next_bytes(&fr, baseline, sizeof(baseline));

    unsigned int iterations = (unsigned int)FR_next_range(&fr, 0, 10);

    for (unsigned int i = 0; i < iterations; ++i) {
        init_fuzzer_once();
        if (FR_remaining(&reader) < 24) {
            return;
        }

        /* Fuzz memory region alignment logic using public macros */
        uintptr_t addr = (uintptr_t)FR_next_u32(&reader);
        size_t size = (size_t)FR_next_range(&reader, 0, 0xFFFF);
        uint32_t pwr = FR_next_range(&reader, 0, 12);
        size_t align = (size_t)(1ULL << pwr);
        
        uintptr_t a_addr = addr;
        size_t a_size = size;
        
        if (align > 0) {
            a_addr = (uintptr_t)ROUND_DOWN(addr, align);
            a_size = (size_t)(ROUND_UP(addr + size, align) - a_addr);
        }
        __ASSERT(a_size >= size, "Aligned size must be at least original size");

        /* Fuzz k_timer_status_get (non-blocking) */
        uint32_t ticks = FR_next_range(&reader, 0, 10);
        k_timer_start(&fuzzer_timer, K_TICKS(ticks), K_NO_WAIT);
        k_timer_stop(&fuzzer_timer);
        uint32_t status = k_timer_status_get(&fuzzer_timer);
        (void)status;

        /* Fuzz uart_irq_is_pending */
        const struct device *u_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(uart0));
        if (u_dev != NULL && device_is_ready(u_dev)) {
            int pending = uart_irq_is_pending(u_dev);
            (void)pending;
        }

        /* Fuzz retained_mem_write */
#if DT_HAS_CHOSEN(zephyr_retained_mem)
        const struct device *rm_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_retained_mem));
        if (device_is_ready(rm_dev)) {
            ssize_t rm_size = retained_mem_size(rm_dev);
            if (rm_size > 0) {
                off_t off = (off_t)FR_next_range(&reader, 0, (uint32_t)rm_size - 1);
                size_t len = (size_t)FR_next_range(&reader, 1, (uint32_t)rm_size - (uint32_t)off);
                if (len > sizeof(fuzzer_data)) {
                    len = sizeof(fuzzer_data);
                }
                FR_next_bytes(&reader, fuzzer_data, len);
                int ret = retained_mem_write(rm_dev, off, fuzzer_data, len);
                (void)ret;
            }
        }
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
