#include <asm/kasan.h>
//#include <linux/kasan.h>
#include <asm/host_ops.h>
#include <linux/export.h>
#ifdef CONFIG_KASAN

extern short kasan_allocated_mem;
extern unsigned long memory_start, memory_end;
unsigned long lkl_kasan_shadow_start;
unsigned long lkl_kasan_shadow_end;
unsigned long lkl_kasan_stack_start;
unsigned long lkl_kasan_stack_end;
unsigned long lkl_kasan_stack_shadow_start;
unsigned long lkl_kasan_stack_shadow_end;
unsigned long readable_size;
void kasan_early_init(void) {
    ;
}

void kasan_init(void) {
    ;
}
 __no_sanitize_address unsigned long lkl_kasan_init(struct lkl_host_operations* ops,
        unsigned long mem_sz,
        unsigned long stack_base,
        unsigned long stack_size) {
    struct lkl_host_operations *lkl_ops = ops;
    lkl_kasan_shadow_start = lkl_ops->mem_alloc(mem_sz); //XXX
    lkl_kasan_shadow_end = lkl_kasan_shadow_start + (mem_sz); //XXX
    kasan_allocated_mem = 1;
    memory_start = (unsigned long)lkl_ops->mem_alloc(mem_sz);
    memory_end = memory_start + mem_sz;

    lkl_kasan_stack_start = stack_base;
    lkl_kasan_stack_end = stack_base + stack_size;

    lkl_kasan_stack_shadow_start = lkl_ops->mem_alloc(stack_size);
    lkl_kasan_stack_shadow_end = lkl_kasan_stack_shadow_start + stack_size;

    return lkl_kasan_stack_start;
}
#else

unsigned long lkl_kasan_init(struct lkl_host_operations* ops, unsigned long mem_sz) {
    return 1;
}
#endif
