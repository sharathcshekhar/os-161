#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <thread.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

#define STACKPAGES    12

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

static bool vm_initialized = false;

typedef enum {
	PG_TLB = 0,
	PG_MEM,
	PG_SWP,
	PG_UNALOC,
	PG_BUSY,
	PG_UNKNOWN,
} page_status_t;

struct pg_table_entry {
	paddr_t ppage;
	vaddr_t vpage;
	page_status_t state;
	off_t swp_offset;
};

struct coremap_t {
	paddr_t ppage; 				/* physical address of this page */
	struct pg_table_entry *pte; /* page table entry that is pointing to this addr */
	/* to be used in swapping */
	bool lru_bit;
	/* unused for now, leak memory */
	bool nxt_pg;
	bool status;
};

struct coremap_t *coremap = NULL;
static int ppages = 0;

void coremap_init(paddr_t lo_ram);

void coremap_init(paddr_t lo_ram)
{
	int i;
	for (i = 0; i < ppages; i++) {
		coremap[i].ppage = lo_ram;
		coremap[i].pte = NULL;
		coremap[i].lru_bit = false;
		coremap[i].nxt_pg = false;
		coremap[i].status = false;
		lo_ram = lo_ram + PAGE_SIZE;
	}
}

void
vm_bootstrap(void)
{
	paddr_t lo_ram, hi_ram;
	int coremap_pages;
	ram_getsize(&lo_ram, &hi_ram);
	ppages = (hi_ram - lo_ram)/PAGE_SIZE;
	coremap = (struct coremap_t *)PADDR_TO_KVADDR(lo_ram);
	coremap_pages = (sizeof(struct coremap_t) * ppages)/(PAGE_SIZE + 1) + 1;
	lo_ram += (coremap_pages * PAGE_SIZE);
	ppages -= coremap_pages;
	coremap_init(lo_ram);
	vm_initialized = true;
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr = 0;
	int i;
	if (!vm_initialized) {
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
	} else {
		spinlock_acquire(&stealmem_lock);
		for (i = 0; i < ppages; i++) {
			if (coremap[i].status == false) {
				coremap[i].status = true;
				addr = coremap[i].ppage;
				break;
			}
		}
		spinlock_release(&stealmem_lock);
	}
	KASSERT(addr != 0);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa == 0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{
	int i;
	if (!vm_initialized) {
		return;
	}
	spinlock_acquire(&stealmem_lock);
	for (i = 0; i < ppages; i++) {
		vaddr_t va = PADDR_TO_KVADDR(coremap[i].ppage);
		if (va == addr) {
			coremap[i].status = false;
			break;
		}
	}
	spinlock_release(&stealmem_lock);
}

void
vm_tlbshootdown_all(void)
{
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	as = curthread->t_addrspace;
	if (as == NULL) {
		/*
		 * No address space set up. This is probably a kernel
		 * fault early in boot. Return EFAULT so as to panic
		 * instead of getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}
