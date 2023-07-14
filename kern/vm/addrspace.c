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
#include <proc.h>
#include "spl.h"
#include "vm_tlb.h"

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */

	as->as_vbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_npages2 = 0;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret, pid_t oldp, pid_t newp, int spl)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */

	// KASSERT(old->as_vbase1==0x400000);
	// KASSERT(old->as_vbase2==0x412000);
	// KASSERT(old->as_npages1==3);
	// KASSERT(old->as_npages2==18);

	newas->as_vbase1 = old->as_vbase1;
	newas->as_npages1 = old->as_npages1;
	newas->as_vbase2 = old->as_vbase2;
	newas->as_npages2 = old->as_npages2;
	newas->ph1 = old->ph1;
	newas->ph2 = old->ph2;
	newas->v = old->v;
	old->v->vn_refcount++;
	newas->initial_offset1=old->initial_offset1;
	newas->initial_offset2=old->initial_offset2;

	prepare_copy_pt(oldp);
	prepare_copy_swap(oldp,newp);
	copy_swap_pages(newp, oldp, spl);
	copy_pt_entries(oldp, newp);
	end_copy_pt(oldp);
	end_copy_swap(newp);

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	if(as->v->vn_refcount==1){
		vfs_close(as->v);
	}
	else{
		as->v->vn_refcount--;
	}


	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;
	int spl;
	spl = splhigh();
	//kprintf("WE ARE RUNNING PROCESS %d\n",curproc->p_pid);
	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	tlb_invalidate_all();//Since the TLB has not a PID field, we must invalidate it each time we activate a new address space
	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	size_t npages, initial_offset;

	/* Align the region. First, the base... */
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	initial_offset=vaddr % PAGE_SIZE;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	memsize = (memsize + initial_offset + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = memsize / PAGE_SIZE;

	/* We don't use these - exceptions about writing a readonly page will be raides by checking the virtual address */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		DEBUG(DB_VM,"Text starts at: 0x%x\n",vaddr);
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		as->initial_offset1=initial_offset;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		DEBUG(DB_VM,"Data starts at: 0x%x\n",vaddr);
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->initial_offset2=initial_offset;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");

	return ENOSYS;
}

int
as_prepare_load(struct addrspace *as)//not needed with on demand paging since we don't load anything without a fault
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)//not needed with on demand paging since we don't load anything without a fault
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

int as_is_ok(){
    struct addrspace *as = proc_getas();
    if(as == NULL)
        return 0;
    if(as->as_vbase1 == 0)
        return 0;
    if(as->as_vbase2 == 0)
        return 0;
    if(as->as_npages1 == 0)
        return 0;
    if(as->as_npages2 == 0)
        return 0;
    return 1;
}

void vm_bootstrap(void){
	swap_init();
	pt_init();
}

void vm_tlbshootdown(const struct tlbshootdown *ts){
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void vm_shutdown(void){
	for(int i=0;i<peps.ptSize;i++){
		if(peps.pt[i].ctl!=0){
			kprintf("Entry%d has not been freed! ctl=%d, pid=%d\n",i,peps.pt[i].ctl,peps.pt[i].pid);
		}
		if(peps.pt[i].page==1){
			kprintf("It looks like some errors with free occurred: entry%d, process %d\n",i,peps.pt[i].pid);
		}
	}

	print_stats();
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	addr = ram_stealmem(npages);

	return addr;
}

vaddr_t alloc_kpages(unsigned npages){

	int spl = splhigh();
	
	paddr_t p;

	spinlock_acquire(&stealmem_lock);
	
	if(!pt_active){
		p = getppages(npages);
	}
	else{
		nkmalloc+=npages;
		spinlock_release(&stealmem_lock);
		p = get_contiguous_pages(npages, spl);
		spinlock_acquire(&stealmem_lock);
	}

	spinlock_release(&stealmem_lock);

	splx(spl);

	return PADDR_TO_KVADDR(p);
}

void free_kpages(vaddr_t addr){

	int spl = splhigh();

	spinlock_acquire(&stealmem_lock);

	if(!pt_active || addr < PADDR_TO_KVADDR(peps.firstfreepaddr)){
		//Currently we accept a memory leak since the cost of having an additional data structure would be more expensive than the potential memory leaks that could occur
		//Alternative: move contiguous in another place (coremap.c?)
	}
	else{
		spinlock_release(&stealmem_lock);
		free_contiguous_pages(addr);
		spinlock_acquire(&stealmem_lock);
	}

	spinlock_release(&stealmem_lock);

	splx(spl);
}

void addrspace_init(void){
	spinlock_init(&stealmem_lock);
	pt_active=0;
}
