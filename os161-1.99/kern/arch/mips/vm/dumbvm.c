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
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include "opt-A2.h"
#include "opt-A3.h"
#include <copyinout.h>
#include <synch.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;

#if OPT_A3

paddr_t lo;
paddr_t hi;

int *coremap = NULL;
bool coremap_created = false;
int num_pages = 0;

// static struct lock *coremap_lock = NULL;

#endif /* OPT_A3 */

void
vm_bootstrap(void)
{
#if OPT_A3
	// coremap_lock = lock_create("coremap_lock");
	ram_getsize(&lo, &hi);
	num_pages = (hi - lo) / PAGE_SIZE;

	coremap = (int *)PADDR_TO_KVADDR(lo);
	for (int i = 0; i < num_pages; i++) {
		coremap[i] = 0;
	}
	lo += ROUNDUP(num_pages * sizeof(int), PAGE_SIZE); //page-size aligned
	num_pages = (hi - lo) / PAGE_SIZE; //updated number of pages
	coremap_created = true;

	// kprintf("lo address at boot is %u\n", (unsigned int)PADDR_TO_KVADDR(lo) );
#endif /* OPT_A3 */
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr  = 0;

#if OPT_A3
	if (coremap_created) {
		// search coremap for continuous block
		// kprintf("allocation %u pages\n", (unsigned int) npages);
		// lock_acquire(coremap_lock);
		spinlock_acquire(&coremap_lock);
		int i = 0;
		while (i < num_pages) {
			unsigned available_pages = 0;
			int j = i;
			while (j < num_pages && coremap[j] == 0 && available_pages < npages) {
				available_pages++;
				j++;
			}
			if (available_pages == npages) {
				addr = lo + i*PAGE_SIZE;
				for (unsigned long z = 0; z < npages; z++) {
					coremap[i+z] = (int)z+1;
					// kprintf("coremap at index %d is %d\n", (int)(i+z), (int)coremap[i+z]);
				}
				// kprintf("alloc addr: %d\n", addr);
				// lock_release(coremap_lock);
				spinlock_release(&coremap_lock);
				return addr;
			}
			i = j+1;
		}
		// lock_release(coremap_lock);
		spinlock_release(&coremap_lock);
		return 0;
	} else {
		spinlock_acquire(&stealmem_lock);

		addr = ram_stealmem(npages);

		spinlock_release(&stealmem_lock);
	}
#else
	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);
	
	spinlock_release(&stealmem_lock);
#endif /* OPT_A3 */

	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void free_kpages_p(paddr_t addr) {
	spinlock_acquire(&coremap_lock);
	int i = (addr - lo) / PAGE_SIZE;
		
		// kprintf("vlo: %u\n", (int) vlo);
		while (true) {
			// kprintf("i: %u\n", i);
			int temp = coremap[i];
			coremap[i] = 0;
			if (i+1 < num_pages && coremap[i+1] > temp) {
				i++;
			} else {
				break;
			}
		}
	spinlock_release(&coremap_lock);
}

void 
free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */
#if OPT_A3 
	// kprintf("addr: %u\n", (int) addr);
	// vaddr_t vlo = PADDR_TO_KVADDR(lo);
	// int i = (addr - vlo) / PAGE_SIZE;
	// lock_acquire(coremap_lock);
	spinlock_acquire(&coremap_lock);
	// paddr_t paddr = addr - MIPS_KSEG0;
	// int i = (paddr - lo) / PAGE_SIZE;
	vaddr_t vlo = PADDR_TO_KVADDR(lo);
	int i = (addr - vlo) / PAGE_SIZE;
	
	// kprintf("vlo: %u\n", (int) vlo);
	while (true) {
		// kprintf("i: %u\n", i);
		int temp = coremap[i];
		coremap[i] = 0;
		if (i+1 < num_pages && coremap[i+1] > temp) {
			i++;
		} else {
			break;
		}
	}
	// lock_release(coremap_lock);
	spinlock_release(&coremap_lock);
#else
	(void)addr;
#endif /* OPT_A3 */
 
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
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

#if OPT_A3
	bool is_code = false;
#else
#endif /* OPT_A3 */

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
#if OPT_A3
	    return EFAULT;
#else
	    panic("dumbvm: got VM_FAULT_READONLY\n");
#endif /* OPT_A3 */
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

#if OPT_A3 
		/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != NULL);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != NULL);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != NULL);

	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	// ugh 
	// KASSERT((as->as_pbase1[0] & PAGE_FRAME) == as->as_pbase1[0]);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	// KASSERT((as->as_pbase2[0] & PAGE_FRAME) == as->as_pbase2[0]);
	// KASSERT((as->as_stackpbase[0] & PAGE_FRAME) == as->as_stackpbase[0]);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) { //code segment
		is_code = true;
		// paddr = (faultaddress - vbase1) + as->as_pbase1;
		int page_num = (faultaddress - vbase1) / PAGE_SIZE;
		paddr = as->as_pbase1[page_num];
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		// paddr = (faultaddress - vbase2) + as->as_pbase2;
		int page_num = (faultaddress - vbase2) / PAGE_SIZE;
		paddr = as->as_pbase2[page_num];
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		// paddr = (faultaddress - stackbase) + as->as_stackpbase;
		int page_num = (faultaddress - stackbase) / PAGE_SIZE;
		paddr = as->as_stackpbase[page_num];
	}
	else {
		return EFAULT;
	}

#else
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
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) { //code segment
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
#endif /* OPT_A3 */

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

#if OPT_A3
		if (is_code && as->load_elf_done) {
			elo &= ~TLBLO_DIRTY;
		}
#endif /* OPT_A3 */

		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

// tlb is full
#if OPT_A3
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;

	if (is_code && as->load_elf_done) {
		elo &= ~TLBLO_DIRTY;
	}
	tlb_random(ehi, elo);
	splx(spl);
	return 0;
#else
	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
#endif /* OPT_A3 */
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

#if OPT_A3
	as->load_elf_done = false;
	as->as_vbase1 = 0;
	as->as_pbase1 = NULL;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = NULL;
	as->as_npages2 = 0;
	as->as_stackpbase = NULL;
#else
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
#endif /* OPT_A3 */
	return as;
}

void
as_destroy(struct addrspace *as)
{
#if OPT_A3 
	// free_kpages_p(as->as_pbase1);
	// free_kpages_p(as->as_pbase2);
	// free_kpages(PADDR_TO_KVADDR(as->as_stackpbase));
	for (size_t i = 0; i < as->as_npages1; i++) {
		free_kpages_p(as->as_pbase1[i]);
	}
	kfree(as->as_pbase1);

	for (size_t i = 0; i < as->as_npages2; i++) {
		free_kpages_p(as->as_pbase2[i]);
	}
	kfree(as->as_pbase2);

	for (size_t i = 0; i < DUMBVM_STACKPAGES; i++) {
		free_kpages_p(as->as_stackpbase[i]);
	}
	kfree(as->as_stackpbase);

#else
	kfree(as);
#endif /* OPT_A3 */
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

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

#if OPT_A3 
	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		as->as_pbase1 = kmalloc(npages * sizeof(paddr_t));
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->as_pbase2 = kmalloc(npages * sizeof(paddr_t));
		return 0;
	}
#else
	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}
#endif /* OPT_A3 */


	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
#if OPT_A3 
	KASSERT(as->as_pbase1 != NULL);
	KASSERT(as->as_pbase2 != NULL);
	KASSERT(as->as_stackpbase == NULL);

	for (size_t i = 0; i < as->as_npages1; i++) {
		as->as_pbase1[i] = getppages(1);
		if (as->as_pbase1[i] == 0) {
			return ENOMEM;
		}
		as_zero_region(as->as_pbase1[i], 1);
	}

	for (size_t i = 0; i < as->as_npages2; i++) {
		as->as_pbase2[i] = getppages(1);
		if (as->as_pbase2[i] == 0) {
			return ENOMEM;
		}
		as_zero_region(as->as_pbase2[i], 1);
	}

	// create a page table for the stack
	as->as_stackpbase = kmalloc(DUMBVM_STACKPAGES * sizeof(paddr_t));
	if (as->as_stackpbase == NULL) {
		return ENOMEM;
	}
	for (size_t i = 0; i < DUMBVM_STACKPAGES; i++) {
		as->as_stackpbase[i] = getppages(1);
		if (as->as_stackpbase[i] == 0) {
			return ENOMEM;
		}
		as_zero_region(as->as_stackpbase[i], 1);
	}
	return 0;

#else
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}
	
	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
#endif /* OPT_A3 */

}

int
as_complete_load(struct addrspace *as)
{
#if OPT_A3
	as->load_elf_done = true;
	as_activate();
#else
	(void)as;
#endif /* OPT_A3 */
	return 0;
}


#if OPT_A2 
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr, char **args, int argsCount)
{
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	vaddr_t argStackAddr[argsCount+1];

  for (int i = argsCount-1; i >= 0; i--) { // put the strings onto the stack
    size_t decrement = strlen(args[i]) + 1;
    decrement = ROUNDUP(decrement, 4);
    *stackptr -= decrement;
    argStackAddr[i] = *stackptr;
    int res = copyoutstr(args[i], (userptr_t)*stackptr, decrement, NULL);
    if (res) {
    	return res;
    }
  }
  argStackAddr[argsCount] = 0;

  for (int i = argsCount; i >=0; i--) { // put the ptr onto the stack
  	size_t decrement = ROUNDUP(sizeof(vaddr_t), 4);
    *stackptr -= decrement;
    int res = copyout(&argStackAddr[i], (userptr_t)*stackptr, decrement);
    if (res) {
    	return res;
    }
  }
	return 0;
}

#else
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}
#endif /* OPT_A2 */

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new == NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;


#if OPT_A3 
	new->as_pbase1 = kmalloc(sizeof(paddr_t) * new->as_npages1);
	if (new->as_pbase1 == NULL) {
		return ENOMEM;
	}
	new->as_pbase2 = kmalloc(sizeof(paddr_t) * new->as_npages2);
	if (new->as_pbase2 == NULL) {
		kfree(new->as_pbase1);
		return ENOMEM;
	}

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	for (size_t i = 0; i < new->as_npages1; i++) {
		memmove((void *)PADDR_TO_KVADDR(new->as_pbase1[i]),
			(const void *)PADDR_TO_KVADDR(old->as_pbase1[i]),
			PAGE_SIZE);
		}
	for (size_t i = 0; i < new->as_npages2; i++) {
		memmove((void *)PADDR_TO_KVADDR(new->as_pbase2[i]),
			(const void *)PADDR_TO_KVADDR(old->as_pbase2[i]),
			PAGE_SIZE);
	}
	for (size_t i = 0; i < DUMBVM_STACKPAGES; i++) {
		memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase[i]),
			(const void *)PADDR_TO_KVADDR(old->as_stackpbase[i]),
			PAGE_SIZE);
	}

#else
	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
#endif /* OPT_A3 */
	*ret = new;
	return 0;
}
