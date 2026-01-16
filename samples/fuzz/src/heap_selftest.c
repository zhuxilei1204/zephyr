#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/sys_heap.h>
#include <string.h>
#include <stdint.h>

/*
 * Dynamic self-tests for Zephyr sys_heap / k_heap.
 *
 * Build/run selftest-only firmware:
 *   west build -b mps2/an385 -p always -- -DCMAKE_C_FLAGS=-DHEAP_SELFTEST_ONLY
 *
 * Optional (will intentionally crash on ASSERT-enabled builds):
 *   -DCMAKE_C_FLAGS='-DHEAP_SELFTEST_ONLY -DHEAP_SELFTEST_MISUSE'
 */

extern int BREAKPOINT(void);

#define ST_HEAP_SIZE 8192
static uint8_t st_mem[ST_HEAP_SIZE] __aligned(64);
static struct k_heap st_kheap;

#ifdef HEAP_SELFTEST_MISUSE
/*
 * Misuse tests may corrupt arbitrary memory when ASSERT is disabled.
 * Place a sentinel region near our heap buffers to make silent scribbles
 * more observable.
 */
static uint8_t st_mem2[ST_HEAP_SIZE] __aligned(64);
static struct k_heap st_kheap2;

static uint32_t st_sentinel[2048];

static volatile uint32_t st_last_round;
static volatile uint32_t st_last_sz;
static volatile uint32_t st_last_off;
static volatile uint32_t st_last_mode;

static void st_sentinel_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(st_sentinel); i++) {
		st_sentinel[i] = 0xA5A50000u ^ (uint32_t)i;
	}
}

static bool st_sentinel_ok(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(st_sentinel); i++) {
		uint32_t exp = 0xA5A50000u ^ (uint32_t)i;
		if (st_sentinel[i] != exp) {
			printk("[SELFTEST_MISUSE] sentinel corrupted at idx=%u val=0x%08x exp=0x%08x\n",
			       (unsigned)i, st_sentinel[i], exp);
			return false;
		}
	}
	return true;
}

static void st_heap2_stress(void)
{
	/* Light stress to surface latent corruption */
	void *tmp[32] = {0};
	for (int i = 0; i < 32; i++) {
		tmp[i] = k_heap_alloc(&st_kheap2, 24 + (i % 7), K_NO_WAIT);
		if (tmp[i]) {
			memset(tmp[i], 0x5A, 24 + (i % 7));
		}
	}
	for (int i = 0; i < 32; i++) {
		k_heap_free(&st_kheap2, tmp[i]);
	}
}

static uint32_t st_prng_state = 0xC001D00Du;

static inline uint32_t st_prng_next(void)
{
	/* xorshift32 */
	uint32_t x = st_prng_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	st_prng_state = x;
	return x;
}
#endif

static void st_fail(const char *msg)
{
	printk("[SELFTEST_FAIL] %s\n", msg);
	__builtin_trap();
}

static void st_expect(bool ok, const char *msg)
{
	if (!ok) {
		st_fail(msg);
	}
}

static void st_validate_kheap(const char *where)
{
	bool ok = sys_heap_validate(&st_kheap.heap);
	if (!ok) {
		printk("[SELFTEST] validate failed at: %s\n", where);
		st_fail("sys_heap_validate");
	}
}

static void st_basic_alloc_free(void)
{
	k_heap_init(&st_kheap, st_mem, sizeof(st_mem));
	st_validate_kheap("basic:init");

	void *p1 = k_heap_alloc(&st_kheap, 1, K_NO_WAIT);
	st_expect(p1 != NULL, "alloc(1) returned NULL");
	memset(p1, 0xA5, 1);
	st_validate_kheap("basic:after alloc(1)");

	void *p2 = k_heap_alloc(&st_kheap, 128, K_NO_WAIT);
	st_expect(p2 != NULL, "alloc(128) returned NULL");
	memset(p2, 0x5A, 128);
	st_validate_kheap("basic:after alloc(128)");

	k_heap_free(&st_kheap, p1);
	st_validate_kheap("basic:after free(p1)");

	k_heap_free(&st_kheap, p2);
	st_validate_kheap("basic:after free(p2)");

	/* ISO C semantics */
	k_heap_free(&st_kheap, NULL);
	st_validate_kheap("basic:after free(NULL)");
}

static void st_realloc_semantics(void)
{
	k_heap_init(&st_kheap, st_mem, sizeof(st_mem));
	st_validate_kheap("realloc:init");

	uint8_t *p = k_heap_alloc(&st_kheap, 64, K_NO_WAIT);
	st_expect(p != NULL, "alloc(64) returned NULL");
	for (size_t i = 0; i < 64; i++) {
		p[i] = (uint8_t)i;
	}
	st_validate_kheap("realloc:after alloc");

	/* Grow */
	uint8_t *p2 = k_heap_realloc(&st_kheap, p, 256, K_NO_WAIT);
	st_expect(p2 != NULL, "realloc grow returned NULL");
	for (size_t i = 0; i < 64; i++) {
		if (p2[i] != (uint8_t)i) {
			st_fail("realloc grow data mismatch");
		}
	}
	st_validate_kheap("realloc:after grow");

	/* Shrink */
	uint8_t *p3 = k_heap_realloc(&st_kheap, p2, 32, K_NO_WAIT);
	st_expect(p3 != NULL, "realloc shrink returned NULL");
	for (size_t i = 0; i < 32; i++) {
		if (p3[i] != (uint8_t)i) {
			st_fail("realloc shrink data mismatch");
		}
	}
	st_validate_kheap("realloc:after shrink");

	/* Special cases */
	void *p4 = k_heap_realloc(&st_kheap, NULL, 16, K_NO_WAIT);
	st_expect(p4 != NULL, "realloc(NULL,16) returned NULL");
	st_validate_kheap("realloc:after realloc(NULL,n)");

	void *p5 = k_heap_realloc(&st_kheap, p4, 0, K_NO_WAIT);
	st_expect(p5 == NULL, "realloc(ptr,0) must return NULL");
	st_validate_kheap("realloc:after realloc(ptr,0)");

	k_heap_free(&st_kheap, p3);
	st_validate_kheap("realloc:after final free");
}

static void st_aligned_alloc(void)
{
	k_heap_init(&st_kheap, st_mem, sizeof(st_mem));
	st_validate_kheap("align:init");

	static const size_t aligns[] = { 0, 4, 8, 16, 32, 64 };

	for (size_t ai = 0; ai < ARRAY_SIZE(aligns); ai++) {
		size_t align = aligns[ai];
		for (size_t sz = 1; sz <= 256; sz += 17) {
			void *p = k_heap_aligned_alloc(&st_kheap, align, sz, K_NO_WAIT);
			st_expect(p != NULL, "aligned_alloc returned NULL");
			if (align != 0) {
				st_expect(((uintptr_t)p % align) == 0, "alignment not satisfied");
			}
			memset(p, 0xCC, sz);
			st_validate_kheap("align:after alloc");
			k_heap_free(&st_kheap, p);
			st_validate_kheap("align:after free");
		}
	}
}

static void st_extreme_sizes(void)
{
	k_heap_init(&st_kheap, st_mem, sizeof(st_mem));
	st_validate_kheap("extreme:init");

	void *p0 = k_heap_alloc(&st_kheap, 0, K_NO_WAIT);
	st_expect(p0 == NULL, "alloc(0) must return NULL");
	st_validate_kheap("extreme:after alloc(0)");

	/* Too-large request should fail cleanly */
	size_t huge = (size_t)-1 / 2;
	void *ph = k_heap_alloc(&st_kheap, huge, K_NO_WAIT);
	st_expect(ph == NULL, "alloc(huge) should return NULL");
	st_validate_kheap("extreme:after alloc(huge)");
}

static void st_fragmentation(void)
{
	k_heap_init(&st_kheap, st_mem, sizeof(st_mem));
	st_validate_kheap("frag:init");

	void *ptrs[16] = {0};
	for (int i = 0; i < 16; i++) {
		ptrs[i] = k_heap_alloc(&st_kheap, 64 + (i * 7), K_NO_WAIT);
		st_expect(ptrs[i] != NULL, "frag alloc returned NULL");
		memset(ptrs[i], (uint8_t)i, 64 + (i * 7));
	}
	st_validate_kheap("frag:after alloc batch");

	for (int i = 0; i < 16; i += 2) {
		k_heap_free(&st_kheap, ptrs[i]);
		ptrs[i] = NULL;
	}
	st_validate_kheap("frag:after freeing evens");

	void *big = k_heap_alloc(&st_kheap, 512, K_NO_WAIT);
	/* big may legitimately fail depending on fragmentation; validate must still hold */
	st_validate_kheap("frag:after big alloc attempt");
	if (big) {
		memset(big, 0xEE, 512);
		k_heap_free(&st_kheap, big);
		st_validate_kheap("frag:after big free");
	}

	for (int i = 0; i < 16; i++) {
		if (ptrs[i]) {
			k_heap_free(&st_kheap, ptrs[i]);
		}
	}
	st_validate_kheap("frag:after cleanup");
}

#ifdef HEAP_SELFTEST_MISUSE
static void st_misuse_expected_crash(void)
{
	/*
	 * Negative/robustness tests (NOT exploitation):
	 * These cases are outside the API contract. With CONFIG_ASSERT=y,
	 * they should typically trap/panic. Without ASSERT, they may corrupt the heap.
	 */

	#ifndef HEAP_SELFTEST_MISUSE_CASE
	#define HEAP_SELFTEST_MISUSE_CASE 1
	#endif

	printk("[SELFTEST_MISUSE] case=%d\n", (int)HEAP_SELFTEST_MISUSE_CASE);

	switch (HEAP_SELFTEST_MISUSE_CASE) {
	case 1: {
		/* double free */
		k_heap_init(&st_kheap, st_mem, sizeof(st_mem));
		void *p = k_heap_alloc(&st_kheap, 32, K_NO_WAIT);
		st_expect(p != NULL, "misuse alloc returned NULL");
		k_heap_free(&st_kheap, p);
		k_heap_free(&st_kheap, p);
		break;
	}
	case 2: {
		/*
		 * cross-heap free (repeated + pseudo-random pointer offsets)
		 *
		 * Goal: make silent corruption more likely/observable in no-ASSERT builds.
		 * We re-init heaps per round to avoid early exhaustion of heap1.
		 */
		st_sentinel_init();

		#ifndef HEAP_SELFTEST_MISUSE_ROUNDS
		#define HEAP_SELFTEST_MISUSE_ROUNDS 200
		#endif
		#ifndef HEAP_SELFTEST_MISUSE_ALLOW_OOB_OFFSET
		#define HEAP_SELFTEST_MISUSE_ALLOW_OOB_OFFSET 1
		#endif

		for (int round = 0; round < HEAP_SELFTEST_MISUSE_ROUNDS; round++) {
			k_heap_init(&st_kheap, st_mem, sizeof(st_mem));
			k_heap_init(&st_kheap2, st_mem2, sizeof(st_mem2));

			/* vary size a bit */
			size_t sz = 16 + (st_prng_next() % 256);
			uint8_t *p = k_heap_alloc(&st_kheap, sz, K_NO_WAIT);
			st_expect(p != NULL, "misuse alloc returned NULL");
			memset(p, 0xA5, sz);

			/* choose offset: sometimes 0, sometimes inside, sometimes beyond */
			size_t mode = st_prng_next() % (HEAP_SELFTEST_MISUSE_ALLOW_OOB_OFFSET ? 3 : 2);
			size_t off;
			if (mode == 0) {
				off = 0;
			} else if (mode == 1) {
				off = 1 + (st_prng_next() % (sz ? sz : 1));
				if (off >= sz) {
					off = sz ? (sz - 1) : 0;
				}
			} else {
				off = 1 + (st_prng_next() % (sz + 64));
			}

			/* Record last parameters for post-mortem debugging. */
			st_last_round = (uint32_t)round;
			st_last_sz = (uint32_t)sz;
			st_last_off = (uint32_t)off;
			st_last_mode = (uint32_t)mode;

			void *q = (void *)(p + off);
			k_heap_free(&st_kheap2, q);

			/* In a non-ASSERT build, corruption often lands in heap2. */
			if (!sys_heap_validate(&st_kheap2.heap)) {
				printk("[SELFTEST_MISUSE] round=%d sz=%u off=%u\n",
				       round, (unsigned)sz, (unsigned)off);
				st_fail("cross-heap/offset free corrupted heap2");
			}

			/* Light stress and global sentinel check for non-heap scribbles. */
			st_heap2_stress();
			if (!st_sentinel_ok()) {
				printk("[SELFTEST_MISUSE] round=%d sz=%u off=%u\n",
				       round, (unsigned)sz, (unsigned)off);
				st_fail("cross-heap/offset free scribbled non-heap memory");
			}
		}
		break;
	}
	case 3: {
		/* interior-pointer free */
		k_heap_init(&st_kheap, st_mem, sizeof(st_mem));
		uint8_t *p = k_heap_alloc(&st_kheap, 64, K_NO_WAIT);
		st_expect(p != NULL, "misuse alloc returned NULL");
		k_heap_free(&st_kheap, p + 1);
		break;
	}
	default:
		st_fail("unknown HEAP_SELFTEST_MISUSE_CASE");
	}

	/* If we got here, ASSERT didn't fire; report what happened. */
	if (!sys_heap_validate(&st_kheap.heap)) {
		st_fail("misuse caused heap corruption (expected in non-ASSERT build)");
	}

	/*
	 * In "observe" mode, do not intentionally crash the system.
	 * This is useful when you want to see whether the misuse leads to
	 * silent corruption (validate failure) vs. apparently surviving.
	 */
	#ifdef HEAP_SELFTEST_MISUSE_OBSERVE
		printk("[SELFTEST_MISUSE] no immediate crash/corruption observed\n");
		return;
	#else
		st_fail("misuse did not crash; check CONFIG_ASSERT/CONFIG_ASSERT_LEVEL");
	#endif
}
#endif

void heap_selftest_run(void)
{
	printk("[SELFTEST] start\n");

	st_basic_alloc_free();
	st_realloc_semantics();
	st_aligned_alloc();
	st_extreme_sizes();
	st_fragmentation();

#ifdef HEAP_SELFTEST_MISUSE
	st_misuse_expected_crash();
#endif

	printk("[SELFTEST_PASS]\n");
}
