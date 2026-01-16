#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#ifdef TARGET_CUSTOM_INSN
#include "lqemu.h"
#endif

#ifndef TARGET_CUSTOM_INSN
int __attribute__((noinline)) BREAKPOINT() {
    for (;;) {}
}
#endif

// 空指针解引用漏洞
void null_pointer_dereference_vulnerability(unsigned int *data, unsigned int size) {
    printf("Null pointer test\n");
    
    int *ptr = NULL;
    int safe_value = 1234;
    
    printf("Safe value before: %d\n", safe_value);
    
    if (size > 0 && data[0] == 0) {
        printf("Triggering null pointer dereference\n");
        
        // 直接解引用空指针 - 肯定崩溃
        *ptr = 42;
    } else if (size > 0) {
        int value = data[0];
        ptr = &value;
        *ptr = 100;
        printf("Safe pointer usage: %d\n", *ptr);
    }
    
    printf("Safe value after: %d\n", safe_value);
}

int LLVMFuzzerTestOneInput(unsigned int *Data, unsigned int Size) {
#ifdef TARGET_CUSTOM_INSN
    libafl_qemu_start_phys((void *)Data, Size);
#endif

    printf("=== Null Pointer Dereference Test ===\n");
    
    if (Size > 0) {
        null_pointer_dereference_vulnerability(Data, Size);
    }

    printf("Test completed!\n");

#ifdef TARGET_CUSTOM_INSN
    libafl_qemu_end(LIBAFL_QEMU_END_OK);
#else
    return BREAKPOINT();
#endif
}

unsigned int FUZZ_INPUT[] = {0};  // 输入0触发空指针解引用

int main(void) {
    printf("Starting null pointer test\n");
    LLVMFuzzerTestOneInput(FUZZ_INPUT, 1);
    return 0;
}