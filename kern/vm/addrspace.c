/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>

#include <spl.h>
#include <spinlock.h>
#include <thread.h>
#include <current.h>
#include <mips/tlb.h>
#include <mips/vm.h>
#include <syscall.h>

#define _STACKPAGES    12

/*
static void as_zero_region(paddr_t paddr, unsigned npages);

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}
*/

int free_vpages(vaddr_t vpage, int npages);

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	as->as_stackpbase = 0;
	as->page_table = NULL; 
	as->heap_base = 0;
	as->cur_brk = 0;
	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new_as;
   	new_as = as_create();
	if (new_as == NULL) {
		return ENOMEM;
	}
	new_as->page_table = kmalloc(sizeof(struct pagetable));
	/* First copy the Page table */	
	struct pagetable *old_pte = old->page_table;
	struct pagetable *new_pte = new_as->page_table;
	new_pte->prev = NULL;
	while (old_pte != NULL) {
		new_pte->entry = old_pte->entry;
		if (old_pte->entry.state != PG_UNALOC) {
			new_pte->entry.ppage = getppages(1);
			KASSERT(new_pte->entry.ppage);
			memmove((void *)PADDR_TO_KVADDR(new_pte->entry.ppage),
						(const void *)PADDR_TO_KVADDR(old_pte->entry.ppage),
						PAGE_SIZE);
		}
		if (old_pte->next == NULL) {
			new_pte->next = NULL;
		} else {
			new_pte->next = kmalloc(sizeof(struct pagetable));
			new_pte->next->prev = new_pte;
			new_pte = new_pte->next;
		}
		old_pte = old_pte->next;
	}

	*ret = new_as;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	struct pagetable *pte = as->page_table;
	while (pte != NULL) {
		struct pagetable *tmp;
		free_coremap(pte->entry.ppage);
		tmp = pte;
		pte = pte->next;
		kfree(tmp);
	}
	kfree(as);
}

void
as_activate(struct addrspace *as)
{
	(void)as;  // suppress warning until code gets written
	int i, spl;
	spl = splhigh();
	for (i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	uint32_t i;	
	if (as->page_table == NULL) {
		as->page_table = kmalloc(sizeof(struct pagetable));
		as->page_table->next = NULL;
		as->page_table->prev = NULL;
		as->page_table->entry.ppage = 0;
		as->page_table->entry.vpage = vaddr;
		as->page_table->entry.ppage = 0;
		as->page_table->entry.state = PG_UNALOC;
		as->page_table->entry.swp_offset = 0;
	   	npages--;
		vaddr += PAGE_SIZE;
	}
	
	struct pagetable *pte = as->page_table;
	while (pte->next != NULL) {
		pte = pte->next;
	}
	for (i = 0; i < npages; i++) {
		pte->next = kmalloc(sizeof(struct pagetable));
		KASSERT(pte->next);
		pte->next->prev = pte;
		pte = pte->next;
		pte->next = NULL;
		pte->entry.vpage = vaddr;
		pte->entry.ppage = 0;
		pte->entry.state = PG_UNALOC;
		vaddr += PAGE_SIZE;
	}
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	struct pagetable *pte = as->page_table;
	int i;
	KASSERT(pte != NULL);
	while (pte->next != NULL) {
		pte = pte->next;
	}
	vaddr_t stack_pg = (USERSTACK - 1) & PAGE_FRAME;
	for (i = 0; i < _STACKPAGES; i++) {
		pte->next = kmalloc(sizeof(struct pagetable));
		KASSERT(pte->next);
		pte->next->prev = pte;
		pte = pte->next;
		pte->next = NULL;
		pte->entry.vpage = stack_pg;
		pte->entry.ppage = 0;
		pte->entry.state = PG_UNALOC;
		stack_pg -= PAGE_SIZE;
	}

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;
	return 0;
}

int
sys_sbrk(intptr_t amount, int32_t *cur_brk)
{
	size_t npages; 
	vaddr_t heap_pg;
	uint32_t i;
	struct pagetable *pte = curthread->t_addrspace->page_table;
	int free_heap = 0;

	*cur_brk = (int32_t)curthread->t_addrspace->cur_brk;
	/*kprintf("sbrk request amount = %u\n", (uint32_t)amount);*/
	
	if (amount == 0) {
		return 0;	
	} else if (amount > 0) {
		/* Check if the existing allocation can satisfy request */
		if (curthread->t_addrspace->cur_brk != curthread->t_addrspace->heap_base) {
			/* at least 1 page has been allocated */
			free_heap = PAGE_SIZE - (curthread->t_addrspace->cur_brk % PAGE_SIZE);
			if (free_heap == PAGE_SIZE) {
				free_heap = 0;	
			}
		}
		if (amount <= free_heap) {
			/* kprintf("Satisfied in existing heap 0x%x\n", curthread->t_addrspace->cur_brk & PAGE_FRAME); */
			curthread->t_addrspace->cur_brk += amount;
			return 0;
		}
		amount -= free_heap;
		npages = 1 + (amount - 1) / PAGE_SIZE;

		while (pte->next != NULL) {
			pte = pte->next;
		}
		
		struct pagetable *heap_pte = pte;
		heap_pg = (curthread->t_addrspace->cur_brk + free_heap) & PAGE_FRAME;
		for (i = 0; i < npages; i++) {
			/* kprintf("Allocating new heap page at 0x%x\n", heap_pg); */
			pte->next = kmalloc(sizeof(struct pagetable));
			if (pte->next == NULL) {
				/* Ran out of memory, no swapping implemented */
				heap_pte = heap_pte->next;
				if (heap_pte == NULL) {
					return ENOMEM;
				}
				heap_pte->prev->next = NULL;
				while (heap_pte->next != NULL) {
					heap_pte = heap_pte->next;
					kfree(heap_pte->prev);
				}
				/* Last node itself */
				kfree(heap_pte);
				return ENOMEM;
			}
			pte->next->prev = pte;
			pte = pte->next;
			pte->next = NULL;
			pte->entry.vpage = heap_pg;
			pte->entry.ppage = 0;
			pte->entry.state = PG_UNALOC;
			heap_pg += PAGE_SIZE;
		}
		curthread->t_addrspace->cur_brk += (amount + free_heap);
		/* kprintf("Setting cur_brk to 0x%x\n", curthread->t_addrspace->cur_brk); */
	} else {
		/* free page operation */
		//KASSERT(1);
		if ((curthread->t_addrspace->cur_brk + amount) < curthread->t_addrspace->heap_base) {
			return EINVAL;
		}
		vaddr_t old_brk_page = curthread->t_addrspace->cur_brk & PAGE_FRAME;
		vaddr_t new_brk_page = (curthread->t_addrspace->cur_brk + amount) & PAGE_FRAME;
		
		int ret = free_vpages(new_brk_page + PAGE_SIZE, (old_brk_page - new_brk_page)/PAGE_SIZE);
		KASSERT(ret == 0);
	}
	return 0;
}

int free_vpages(vaddr_t vpage, int npages)
{
	struct pagetable *pte = curthread->t_addrspace->page_table;
	
	while (pte != NULL || npages != 0) {
		if (pte->entry.vpage == vpage) {
			struct pagetable *tmp;
			free_kpages(vpage);
			vpage += PAGE_SIZE;
			npages--;
			/* unlink the pte from the table and free it */
			tmp = pte;
			pte = pte->next;
			/* heap entry can never be the first entry in the page table */
			KASSERT(tmp->prev != NULL);
			if (tmp->next != NULL){
				tmp->prev->next = tmp->next;
				tmp->next->prev = tmp->prev;
			}
			kfree(tmp);
		} else {
			pte = pte->next;
		}
	}
	return npages;
}
