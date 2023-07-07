#include "segments.h"

/**
 * This function reads a page from the elf file into the provided virtual address.
 * 
 * @param v; the vnode of the elf file
 * @param offset: the offset used inside the elf file
 * @param vaddr: the virtual address in which we can store the read data
 * @param memsize: the amount of memory to read
 * @param filesize: the amount of memory to read
 * 
 * @return 0 if everything goes fine, otherwise the error code returned from VOP_READ
 * 
*/
static
int
load_elf_page(struct vnode *v,
	     off_t offset, vaddr_t vaddr,
	     size_t memsize, size_t filesize)
{
	struct iovec iov;
	struct uio u;
	int result;

	#if OPT_TEST
	void *kbuf;
	#endif

	if (filesize > memsize) {
		kprintf("ELF: warning: segment filesize > segment memsize\n");
		filesize = memsize;
	}

	DEBUG(DB_VM,"ELF: Loading %lu bytes to 0x%lx\n",(unsigned long) filesize, (unsigned long) vaddr);

	#if OPT_TEST
	uio_kinit(&iov, &u, kbuf, memsize, offset, UIO_READ);
	#else
	iov.iov_ubase = (userptr_t)vaddr;
	iov.iov_len = memsize;		 // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = filesize;          // amount to read from the file
	u.uio_offset = offset;
	u.uio_segflg = UIO_SYSSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = NULL;
	#endif

	#if OPT_TEST
	result = VOP_READ(v, &u);
	#else
	result = VOP_READ(v, &u);
	#endif

	if (result) {
		return result;
	}

	#if OPT_TEST
	copyout();
	#endif

	/*
	 * If memsize > filesize, the remaining space should be
	 * zero-filled. There is no need to do this explicitly,
	 * because the VM system should provide pages that do not
	 * contain other processes' data, i.e., are already zeroed.
	 *
	 * During development of your VM system, it may have bugs that
	 * cause it to (maybe only sometimes) not provide zero-filled
	 * pages, which can cause user programs to fail in strange
	 * ways. Explicitly zeroing program BSS may help identify such
	 * bugs, so the following disabled code is provided as a
	 * diagnostic tool. Note that it must be disabled again before
	 * you submit your code for grading.
	 */
#if 0
	{
		size_t fillamt;

		fillamt = memsize - filesize;
		if (fillamt > 0) {
			DEBUG(DB_EXEC, "ELF: Zero-filling %lu more bytes\n",
			      (unsigned long) fillamt);
			u.uio_resid += fillamt;
			result = uiomovezeros(fillamt, &u);
		}
	}
#endif

	return result;
}

int load_page(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    int swap, result;
    struct addrspace *as;
    int sz=PAGE_SIZE;

	as = proc_getas();

    swap = load_swap(vaddr, pid, paddr); //we check if the page was already read from the elf, i.e. it currently is stored in the swapfile.

    if(swap){
        return 0; //load_swap takes care of loading too, so we just return.
    }

    //If we arrive here the page wasn't found in the swapfile, so we must read it from the elf file.

	/**
	 * We check if the virtual address provided belongs to the text segment
	*/
    if(vaddr>=as->as_vbase1 && vaddr <= as->as_vbase1 + as->as_npages1 * PAGE_SIZE ){

		DEBUG(DB_VM,"\nLOADING CODE: ");

		vaddr_t lastaddr = as->as_vbase1+(as->as_npages1-1)*PAGE_SIZE; //We compute the last virtual address of the segment. We need to do it to understand if we need to zero-fill the page or not (since the last page could have internal fragmentation).

		add_pt_type_fault(DISK);//Update statistics

        if(as->ph1.p_memsz%PAGE_SIZE!=0 && vaddr == lastaddr){ //If as->ph1.p_memsz%PAGE_SIZE=0, the file size if a multiple of PAGE_SIZE and so we don't need to zero fill it. Otherwise, if we try to access the last virtual page we need to clear it before loading.
			bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);//To avoid additional TLB faults, we pretend that the physical address provided belongs to the kernel. In this way, the address tranlation will just be vaddr-0x80000000.
            sz=as->ph1.p_memsz - (as->as_npages1-1)*PAGE_SIZE;//We compute the size of the last page.
        }
        result = load_elf_page(as->v, as->ph1.p_offset+(vaddr - as->as_vbase1), PADDR_TO_KVADDR(paddr), sz, sz);//We load the page
		if (result) {
            panic("Error while reading the text segment");
		}

		add_pt_type_fault(ELF);//Update statistics

        return 0;
    }

    /**
	 * We check if the virtual address provided belongs to the data segment. We use the same logic seen for text segment.
	*/
    if(vaddr>=as->as_vbase2 && vaddr <= as->as_vbase2 + as->as_npages2 * PAGE_SIZE ){

		DEBUG(DB_VM,"\nLOADING DATA, virtual = 0x%x, physical = 0x%x\n",vaddr,paddr);

		vaddr_t lastaddr = as->as_vbase2+(as->as_npages2-1)*PAGE_SIZE;

		add_pt_type_fault(DISK);

		if(as->ph2.p_memsz%PAGE_SIZE!=0 && vaddr == lastaddr){
			bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
            sz=as->ph2.p_memsz - (as->as_npages2-1)*PAGE_SIZE;
        }
        result = load_elf_page(as->v, as->ph2.p_offset+(vaddr - as->as_vbase2),PADDR_TO_KVADDR(paddr),
				      PAGE_SIZE, PAGE_SIZE);
		if (result) {
            panic("Error while reading the data segment");
		}
		// for(int i=0;i<PAGE_SIZE;i++){
		// 	kprintf("%c",kbuf[i]);
		// }

		add_pt_type_fault(ELF);

        return 0;
    }

    /**
	 * We check if the virtual address provided belongs to the text segment.
	 * The check is performed in this way since the stack grows up from 0x80000000 (excluded)
	*/
    if(vaddr<USERSTACK){

		DEBUG(DB_VM,"\nLOADING STACK: ");

		DEBUG(DB_VM,"ELF: Loading 4096 bytes to 0x%lx\n",(unsigned long) vaddr);

        //this time we just 0-fill the page, so no need to perform any kind of load.
        bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
		
		add_pt_type_fault(ZEROED);//update statistics

        return 0;
    }

    /**
	 * Error (access outside the address space)
     * End the program for illegal access
	*/
    kprintf("-------SEGMENTATION FAULT---------");

    sys__exit(-1);

    return -1;
}