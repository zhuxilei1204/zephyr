#include <stdio.h>
#include <string.h>

#ifdef TARGET_CUSTOM_INSN
#include "lqemu.h"
#endif

#ifndef TARGET_CUSTOM_INSN
int __attribute__((noinline)) BREAKPOINT() {
    for (;;) {}
}
#endif

// 除零错误漏洞函数
void divide_by_zero_vulnerability(int divisor) {
    printf("Attempting division: 100 / %d\n", divisor);
    
    // 直接进行除法运算，如果除数为0就会触发错误
    int result = 100 / divisor;
    
    printf("Division result: %d\n", result);
}

int LLVMFuzzerTestOneInput(unsigned int *Data, unsigned int Size) {
#ifdef TARGET_CUSTOM_INSN
    libafl_qemu_start_phys((void *)Data, Size);
#endif

    /* 根据输入数据判断是否触发除零错误 */
    if (Size > 0) {
        int divisor = (int)Data[0];
        
        // 如果输入的第一个值是0，就触发除零错误
        if (divisor == 0) {
            printf("Triggering divide-by-zero vulnerability\n");
            divide_by_zero_vulnerability(divisor);
        } else {
            printf("Safe division with divisor: %d\n", divisor);
            int result = 100 / divisor;
            printf("Result: %d\n", result);
        }
    }

    printf("Hello World!\n");

#ifdef TARGET_CUSTOM_INSN
    libafl_qemu_end(LIBAFL_QEMU_END_OK);
#else
    return BREAKPOINT();
#endif
}

// 单个fuzz输入：0会触发除零错误，其他值安全
unsigned int FUZZ_INPUT[] = {0};

int main(void) {
    LLVMFuzzerTestOneInput(FUZZ_INPUT, 1);
    return 0;
}