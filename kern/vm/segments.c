#include "segments.h"

/**
 * This function reads a page from the elf file into the provided virtual address.
 * 
 * @param v: the vnode of the elf file
 * @param offset: the offset used inside the elf file
 * @param vaddr: the virtual address in which we can store the read data
 * @param memsize: the amount of memory to read
 * @param filesize: the amount of memory to read
 * 
 * @return 0 if everything goes fine, otherwise the error code returned from VOP_READ
 * 
*/
#if OPT_PROJECT
static
int
load_elf_page(struct vnode *v,
	     off_t offset, vaddr_t vaddr,
	     size_t memsize, size_t filesize)
{

	// P(peps.sem);
	struct iovec iov;
	struct uio u;
	int result;

	if (filesize > memsize) {
		kprintf("ELF: warning: segment filesize > segment memsize\n");
		filesize = memsize;
	}

	DEBUG(DB_VM,"ELF: Loading %lu bytes to 0x%lx\n",(unsigned long) filesize, (unsigned long) vaddr);

	/**
	 * We can't use uio_kinit, since it doesn't allow to set a different value for iov_len and uio_resid
	*/

	iov.iov_ubase = (userptr_t)vaddr;
	iov.iov_len = memsize;		 // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = filesize;          // amount to read from the file
	u.uio_offset = offset;
	u.uio_segflg = UIO_SYSSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = NULL;

	result = VOP_READ(v, &u);//We read

	// V(peps.sem);

	return result;
}

int load_page(vaddr_t vaddr, pid_t pid, paddr_t paddr, int spl){

    int swap_found, result;
    struct addrspace *as;
    int sz=PAGE_SIZE, memsz=PAGE_SIZE;
	size_t additional_offset=0;

	as = proc_getas();

    swap_found = load_swap(vaddr, pid, paddr, spl); //we check if the page was already read from the elf, i.e. it currently is stored in the swapfile.

    if(swap_found){
        return 0; //load_swap takes care of loading too, so we just return.
    }

	// print_list(pid);

	//kprintf("Process %d tries to read ELF\n",pid);

    //If we arrive here the page wasn't found in the swapfile, so we must read it from the elf file.

	/**
	 * We check if the virtual address provided belongs to the text segment
	*/
    if(vaddr>=as->as_vbase1 && vaddr <= as->as_vbase1 + as->as_npages1 * PAGE_SIZE ){

		DEBUG(DB_VM,"\nLOADING CODE: ");

		//vaddr_t lastaddr = as->as_vbase1+(as->as_npages1-1)*PAGE_SIZE; //We compute the last virtual address of the segment. We need to do it to understand if we need to zero-fill the page or not (since the last page could have internal fragmentation).

		add_pt_type_fault(DISK);//Update statistics

		if(as->initial_offset1!=0 && (vaddr - as->as_vbase1)==0){
			bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
			additional_offset=as->initial_offset1;
			if(as->ph1.p_filesz>=PAGE_SIZE-additional_offset){
				sz=PAGE_SIZE-additional_offset;
			}
			else{
				sz=as->ph1.p_filesz;
			}
		}
		else{

			if(as->ph1.p_filesz+as->initial_offset1 - (vaddr - as->as_vbase1)<PAGE_SIZE){ //If as->ph1.p_memsz%PAGE_SIZE=0, the file size if a multiple of PAGE_SIZE and so we don't need to zero fill it. Otherwise, if we try to access the last virtual page we need to clear it before loading.
				bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);//To avoid additional TLB faults, we pretend that the physical address provided belongs to the kernel. In this way, the address tranlation will just be vaddr-0x80000000.
				sz=as->ph1.p_filesz+as->initial_offset1 - (vaddr - as->as_vbase1);//We compute the file size of the last page.
				memsz=as->ph1.p_memsz+as->initial_offset1 - (vaddr - as->as_vbase1);//We compute the memory size of the last page.
			}

			if((int)(as->ph1.p_filesz+as->initial_offset1) - (int)(vaddr - as->as_vbase1)<0){//This check is fundamental to avoid issues with programs that have filesz<memsz. In fact, without this check we wouldn't zero the page, causing errors. For a deeper understanding, try to debug testbin/zero analyzing the difference between memsz and filesz.
				bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
				// kprintf("LOAD ELF in 0x%x (virtual: 0x%x) for process %d\n",paddr, vaddr, pid);
				return 0;//We directly return to avoid performing a read of 0 bytes
			}
		}

		// kprintf("LOAD ELF in 0x%x (virtual: 0x%x) for process %d\n",paddr, vaddr, pid);


        result = load_elf_page(as->v, as->ph1.p_offset+(vaddr - as->as_vbase1), PADDR_TO_KVADDR(paddr+additional_offset), memsz, sz-additional_offset);//We load the page
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

		add_pt_type_fault(DISK);

		if(as->initial_offset2!=0 && (vaddr - as->as_vbase2)==0){
			bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
			additional_offset=as->initial_offset2;
			if(as->ph2.p_filesz>=PAGE_SIZE-additional_offset){
				sz=PAGE_SIZE-additional_offset;
			}
			else{
				sz=as->ph2.p_filesz;
			}
		}
		else{
			if(as->ph2.p_filesz+as->initial_offset2 - (vaddr - as->as_vbase2)<PAGE_SIZE){ //If as->ph1.p_memsz%PAGE_SIZE=0, the file size if a multiple of PAGE_SIZE and so we don't need to zero fill it. Otherwise, if we try to access the last virtual page we need to clear it before loading.
				bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);//To avoid additional TLB faults, we pretend that the physical address provided belongs to the kernel. In this way, the address tranlation will just be vaddr-0x80000000.
				sz=as->ph2.p_filesz+as->initial_offset2 - (vaddr - as->as_vbase2);//We compute the file size of the last page.
				memsz=as->ph2.p_memsz+as->initial_offset2 - (vaddr - as->as_vbase2);//We compute the memory size of the last page.
			}

			if((int)(as->ph2.p_filesz+as->initial_offset2) - (int)(vaddr - as->as_vbase2)<0){//This check is fundamental to avoid issues with programs that have filesz<memsz. In fact, without this check we wouldn't zero the page, causing errors. For a deeper understanding, try to debug testbin/zero analyzing the difference between memsz and filesz.
				bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
				add_pt_type_fault(ELF);
				// kprintf("LOAD ELF in 0x%x (virtual: 0x%x) for process %d\n",paddr, vaddr, pid);
				return 0;//We directly return to avoid performing a read of 0 bytes
			}
		}

        result = load_elf_page(as->v, as->ph2.p_offset+(vaddr - as->as_vbase2),	PADDR_TO_KVADDR(paddr+additional_offset),memsz, sz);//We load the page
		if (result) {
            panic("Error while reading the data segment");
		}
		// for(int i=0;i<PAGE_SIZE;i++){
		// 	kprintf("%c",kbuf[i]);
		// }

		add_pt_type_fault(ELF);

		// kprintf("LOAD ELF in 0x%x (virtual: 0x%x) for process %d\n",paddr, vaddr, pid);

        return 0;
    }

    /**
	 * We check if the virtual address provided belongs to the text segment.
	 * The check is performed in this way since the stack grows up from 0x80000000 (excluded)
	*/
    if(vaddr>as->as_vbase2 + as->as_npages2 * PAGE_SIZE && vaddr<USERSTACK){

		DEBUG(DB_VM,"\nLOADING STACK: ");

		DEBUG(DB_VM,"ELF: Loading 4096 bytes to 0x%lx\n",(unsigned long) vaddr);

        //this time we just 0-fill the page, so no need to perform any kind of load.
        bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
		
		add_pt_type_fault(ZEROED);//update statistics

		// kprintf("LOAD ELF in 0x%x (virtual: 0x%x) for process %d\n",paddr, vaddr, pid);

        return 0;
    }

    /**
	 * Error (access outside the address space)
     * End the program for illegal access
	*/
    kprintf("SEGMENTATION FAULT: process %d accessed 0x%x\n",pid,vaddr);

    sys__exit(-1);

    return -1;
}
#endif