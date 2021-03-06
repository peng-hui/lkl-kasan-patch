/*
 * This file contains some kasan initialization code.
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/pfn.h>

#include <asm/page.h>
#include <asm/pgalloc.h>

/*
 * This page serves two purposes:
 *   - It used as early shadow memory. The entire shadow region populated
 *     with this page, before we will be able to setup normal shadow memory.
 *   - Latter it reused it as zero shadow to cover large ranges of memory
 *     that allowed to access, but not handled by kasan (vmalloc/vmemmap ...).
 */
unsigned char kasan_zero_page[PAGE_SIZE] __page_aligned_bss;

#ifdef CONFIG_MMU
#if CONFIG_PGTABLE_LEVELS > 4
p4d_t kasan_zero_p4d[PTRS_PER_P4D] __page_aligned_bss;
#endif
#if CONFIG_PGTABLE_LEVELS > 3
pud_t kasan_zero_pud[PTRS_PER_PUD] __page_aligned_bss;
#endif
#if CONFIG_PGTABLE_LEVELS > 2
pmd_t kasan_zero_pmd[PTRS_PER_PMD] __page_aligned_bss;
#endif


pte_t kasan_zero_pte[PTRS_PER_PTE] __page_aligned_bss;
#endif

static __init void *early_alloc(size_t size, int node)
{
	return memblock_virt_alloc_try_nid(size, size, __pa(MAX_DMA_ADDRESS),
					BOOTMEM_ALLOC_ACCESSIBLE, node);
}

#ifdef CONFIG_MMU
static void __init zero_pte_populate(pmd_t *pmd, unsigned long addr,
				unsigned long end)
{
	pte_t *pte = pte_offset_kernel(pmd, addr);
	pte_t zero_pte;

	zero_pte = pfn_pte(PFN_DOWN(__pa_symbol(kasan_zero_page)), PAGE_KERNEL);
	zero_pte = pte_wrprotect(zero_pte);

	while (addr + PAGE_SIZE <= end) {
		set_pte_at(&init_mm, addr, pte, zero_pte);
		addr += PAGE_SIZE;
		pte = pte_offset_kernel(pmd, addr);
	}
}

static void __init zero_pmd_populate(pud_t *pud, unsigned long addr,
				unsigned long end)
{
	pmd_t *pmd = pmd_offset(pud, addr);
	unsigned long next;

	do {
		next = pmd_addr_end(addr, end);

		if (IS_ALIGNED(addr, PMD_SIZE) && end - addr >= PMD_SIZE) {
			pmd_populate_kernel(&init_mm, pmd, lm_alias(kasan_zero_pte));
			continue;
		}

		if (pmd_none(*pmd)) {
			pmd_populate_kernel(&init_mm, pmd,
					early_alloc(PAGE_SIZE, NUMA_NO_NODE));
		}
		zero_pte_populate(pmd, addr, next);
	} while (pmd++, addr = next, addr != end);
}

static void __init zero_pud_populate(p4d_t *p4d, unsigned long addr,
				unsigned long end)
{
	pud_t *pud = pud_offset(p4d, addr);
	unsigned long next;

	do {
		next = pud_addr_end(addr, end);
		if (IS_ALIGNED(addr, PUD_SIZE) && end - addr >= PUD_SIZE) {
			pmd_t *pmd;

			pud_populate(&init_mm, pud, lm_alias(kasan_zero_pmd));
			pmd = pmd_offset(pud, addr);
			pmd_populate_kernel(&init_mm, pmd, lm_alias(kasan_zero_pte));
			continue;
		}

		if (pud_none(*pud)) {
			pud_populate(&init_mm, pud,
				early_alloc(PAGE_SIZE, NUMA_NO_NODE));
		}
		zero_pmd_populate(pud, addr, next);
	} while (pud++, addr = next, addr != end);
}

static void __init zero_p4d_populate(pgd_t *pgd, unsigned long addr,
				unsigned long end)
{
	p4d_t *p4d = p4d_offset(pgd, addr);
	unsigned long next;

	do {
		next = p4d_addr_end(addr, end);
		if (IS_ALIGNED(addr, P4D_SIZE) && end - addr >= P4D_SIZE) {
			pud_t *pud;
			pmd_t *pmd;

			p4d_populate(&init_mm, p4d, lm_alias(kasan_zero_pud));
			pud = pud_offset(p4d, addr);
			pud_populate(&init_mm, pud, lm_alias(kasan_zero_pmd));
			pmd = pmd_offset(pud, addr);
			pmd_populate_kernel(&init_mm, pmd,
						lm_alias(kasan_zero_pte));
			continue;
		}

		if (p4d_none(*p4d)) {
			p4d_populate(&init_mm, p4d,
				early_alloc(PAGE_SIZE, NUMA_NO_NODE));
		}
		zero_pud_populate(p4d, addr, next);
	} while (p4d++, addr = next, addr != end);
}


/**
 * kasan_populate_zero_shadow - populate shadow memory region with
 *                               kasan_zero_page
 * @shadow_start - start of the memory range to populate
 * @shadow_end   - end of the memory range to populate
 */
void __init kasan_populate_zero_shadow(const void *shadow_start,
				const void *shadow_end)
{
	unsigned long addr = (unsigned long)shadow_start;
	unsigned long end = (unsigned long)shadow_end;
	pgd_t *pgd = pgd_offset_k(addr);
	unsigned long next;

	do {
		next = pgd_addr_end(addr, end);

		if (IS_ALIGNED(addr, PGDIR_SIZE) && end - addr >= PGDIR_SIZE) {
			p4d_t *p4d;
			pud_t *pud;
			pmd_t *pmd;

			/*
			 * kasan_zero_pud should be populated with pmds
			 * at this moment.
			 * [pud,pmd]_populate*() below needed only for
			 * 3,2 - level page tables where we don't have
			 * puds,pmds, so pgd_populate(), pud_populate()
			 * is noops.
			 *
			 * The ifndef is required to avoid build breakage.
			 *
			 * With 5level-fixup.h, pgd_populate() is not nop and
			 * we reference kasan_zero_p4d. It's not defined
			 * unless 5-level paging enabled.
			 *
			 * The ifndef can be dropped once all KASAN-enabled
			 * architectures will switch to pgtable-nop4d.h.
			 */
#ifndef __ARCH_HAS_5LEVEL_HACK
			pgd_populate(&init_mm, pgd, lm_alias(kasan_zero_p4d));
#endif
			p4d = p4d_offset(pgd, addr);
			p4d_populate(&init_mm, p4d, lm_alias(kasan_zero_pud));
			pud = pud_offset(p4d, addr);
			pud_populate(&init_mm, pud, lm_alias(kasan_zero_pmd));
			pmd = pmd_offset(pud, addr);
			pmd_populate_kernel(&init_mm, pmd, lm_alias(kasan_zero_pte));
			continue;
		}

		if (pgd_none(*pgd)) {
			pgd_populate(&init_mm, pgd,
				early_alloc(PAGE_SIZE, NUMA_NO_NODE));
		}
		zero_p4d_populate(pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}
#endif
