#include "segments.h"

static
int
load_elf_page(struct addrspace *as, struct vnode *v,
	     off_t offset, vaddr_t vaddr,
	     size_t memsize, size_t filesize,
	     int is_executable)
{
	struct iovec iov;
	struct uio u;
	int result;

	(void)as;
	(void)is_executable;

	#if OPT_TEST
	void *kbuf;
	#endif

	if (filesize > memsize) {
		kprintf("ELF: warning: segment filesize > segment memsize\n");
		filesize = memsize;
	}

	// kprintf("ELF: Loading %lu bytes to 0x%lx\n",
	//      (unsigned long) filesize, (unsigned long) vaddr);

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

    swap = load_swap(vaddr, pid, paddr);

    if(swap){
        return 0;
    }

    //load from elf

    //text segment
    if(vaddr>=as->as_vbase1 && vaddr <= as->as_vbase1 + as->as_npages1 * PAGE_SIZE ){

		//kprintf("\nLOADING CODE: ");

		vaddr_t lastaddr = as->as_vbase1+(as->as_npages1-1)*PAGE_SIZE;

		add_pt_type_fault(DISK);

        if(as->ph1.p_memsz%PAGE_SIZE!=0 && vaddr == lastaddr){
			bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
            sz=as->ph1.p_memsz;
			while(sz>PAGE_SIZE){
				sz -= PAGE_SIZE;
			}
        }
        result = load_elf_page(as, as->v, as->ph1.p_offset+(vaddr - as->as_vbase1), PADDR_TO_KVADDR(paddr),
				      sz, sz,
				      as->ph1.p_flags & PF_X);
		if (result) {
            panic("Error while reading the text segment");
		}
        //as->ph1.p_offset+=PAGE_SIZE;
		//as->ph1.p_vaddr+=PAGE_SIZE;
        //as->ph1.p_memsz -= sz;
        //as->ph1.p_filesz -= sz;

		add_pt_type_fault(ELF);

        return 0;
    }

    //data segment
    if(vaddr>=as->as_vbase2 && vaddr <= as->as_vbase2 + as->as_npages2 * PAGE_SIZE ){

		//kprintf("\nLOADING DATA, virtual = 0x%x, physical = 0x%x\n",vaddr,paddr);

		//char *kbuf;

		//kbuf = kmalloc(PAGE_SIZE);

		vaddr_t lastaddr = as->as_vbase2+(as->as_npages2-1)*PAGE_SIZE;

		add_pt_type_fault(DISK);

		if(as->ph2.p_memsz%PAGE_SIZE!=0 && vaddr == lastaddr){
			bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
            sz=as->ph2.p_memsz;
			while(sz>PAGE_SIZE){
				sz -= PAGE_SIZE;
			}
        }
        result = load_elf_page(as, as->v, as->ph2.p_offset+(vaddr - as->as_vbase2),PADDR_TO_KVADDR(paddr),
				      PAGE_SIZE, PAGE_SIZE,
				      as->ph2.p_flags & PF_X);
		if (result) {
            panic("Error while reading the data segment");
		}
		// for(int i=0;i<PAGE_SIZE;i++){
		// 	kprintf("%c",kbuf[i]);
		// }
        //as->ph2.p_offset+=PAGE_SIZE;
		//as->ph2.p_vaddr+=PAGE_SIZE;
		//as->ph2.p_memsz -= sz;
        //as->ph2.p_filesz -= sz;

		add_pt_type_fault(ELF);

        return 0;
    }

    //stack segment
    //the check is performed in this way since the stack grows up from 0x80000000
    if(vaddr<=USERSTACK){

		//kprintf("\nLOADING STACK: ");

		// kprintf("ELF: Loading 4096 bytes to 0x%lx\n",
	    //   (unsigned long) vaddr);

        //this time we just 0-fill the page, so no need to perform any kind of load.
        bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
		add_pt_type_fault(ZEROED);

        return 0;
    }

    //Error (out of range)
    //End the program for illegal access

    kprintf("-------SEGMENTATION FAULT---------");

    sys__exit(-1);

    return -1;
}