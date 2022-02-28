#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <asm/kasan.h>
#include <linux/kasan.h>

unsigned long memory_start, memory_end;
static unsigned long _memory_start, mem_size;
short kasan_allocated_mem;

void *empty_zero_page;

void __init bootmem_init(unsigned long mem_sz)
{
	unsigned long bootmap_size;

	mem_size = mem_sz;
#ifdef CONFIG_KASAN
        kasan_allocated_mem = kasan_allocated_mem;
#else
        kasan_allocated_mem = 0;
#endif
        if(kasan_allocated_mem != 1) {
            _memory_start = (unsigned long)lkl_ops->mem_alloc(mem_size);
            memory_start = _memory_start;
            memory_end = memory_start + mem_size;
        }
        else {
            _memory_start = memory_start;
            BUG_ON(memory_end != memory_start + mem_size);
        }
	BUG_ON(!memory_start);


	if (PAGE_ALIGN(memory_start) != memory_start) {
		mem_size -= PAGE_ALIGN(memory_start) - memory_start;
		memory_start = PAGE_ALIGN(memory_start);
		mem_size = (mem_size / PAGE_SIZE) * PAGE_SIZE;
	}
	pr_info("bootmem address range: 0x%lx - 0x%lx\n", memory_start,
		memory_start+mem_size);
#ifdef CONFIG_KASAN
        pr_info("kasan shadow range: 0x%lx - 0x%lx\n", lkl_kasan_shadow_start,
                lkl_kasan_shadow_end);
        pr_info("mem_base: 0x%lx\n", memory_start);
        pr_info("shadow for mem_base: 0x%lx\n", kasan_mem_to_shadow(memory_start));
        pr_info("KASAN_SHADOW_START:  0x%lx\n", KASAN_SHADOW_START);
        pr_info("KASAN_SHADOW_END:    0x%lx\n", KASAN_SHADOW_END);
#endif
	/*
	 * Give all the memory to the bootmap allocator, tell it to put the
	 * boot mem_map at the start of memory.
	 */
	max_low_pfn = virt_to_pfn(memory_end);
	min_low_pfn = virt_to_pfn(memory_start);
	bootmap_size = init_bootmem_node(NODE_DATA(0), min_low_pfn, min_low_pfn,
					 max_low_pfn);

	/*
	 * Free the usable memory, we have to make sure we do not free
	 * the bootmem bitmap so we then reserve it after freeing it :-)
	 */
	free_bootmem(memory_start, mem_size);
	reserve_bootmem(memory_start, bootmap_size, BOOTMEM_DEFAULT);

	empty_zero_page = alloc_bootmem_pages(PAGE_SIZE);
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

	{
		unsigned long zones_size[MAX_NR_ZONES] = {0, };

		zones_size[ZONE_NORMAL] = (mem_size) >> PAGE_SHIFT;
		free_area_init(zones_size);
	}
}

void __init mem_init(void)
{
	max_mapnr = (((unsigned long)high_memory) - PAGE_OFFSET) >> PAGE_SHIFT;
	/* this will put all memory onto the freelists */
	totalram_pages = free_all_bootmem();
	pr_info("Memory available: %luk/%luk RAM\n",
		(nr_free_pages() << PAGE_SHIFT) >> 10, mem_size >> 10);
}

/*
 * In our case __init memory is not part of the page allocator so there is
 * nothing to free.
 */
void free_initmem(void)
{
}

void free_mem(void)
{
	lkl_ops->mem_free((void *)_memory_start);
}
