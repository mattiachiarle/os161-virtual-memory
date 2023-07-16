#include "segments.h"

/**
 * This function reads a page from the elf file into the provided virtual address.
 * 
 * @param v: the vnode of the elf file
 * @param offset: the offset used inside the elf file
 * @param vaddr: the virtual address in which we can store the read data
 * @param memsize: the memory size to read
 * @param filesize: the file size to read
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

	struct iovec iov;
	struct uio u;
	int result;

	if (filesize > memsize) {
		kprintf("ELF: warning: segment filesize > segment memsize\n");
		filesize = memsize;
	}

	DEBUG(DB_VM,"ELF: Loading %lu bytes to 0x%lx\n",(unsigned long) filesize, (unsigned long) vaddr);

	/**
	 * We can't use uio_kinit, since it doesn't allow to set a different value for iov_len and uio_resid (which is fundamental for us. See testbin/zero for additional details).
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

	return result;
}

int load_page(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    int swap_found, result;
    struct addrspace *as;
    int sz=PAGE_SIZE, memsz=PAGE_SIZE;
	size_t additional_offset=0;

    swap_found = load_swap(vaddr, pid, paddr); //we check if the page was already read from the elf, i.e. it currently is stored in the swapfile.

    if(swap_found){
        return 0; //load_swap takes care of loading too, so we just return.
    }

	as = proc_getas();

	#if OPT_DEBUG
	print_list(pid);
	#endif

	DEBUG(DB_VM,"Process %d tries to read ELF\n",pid);

    //If we arrive here the page wasn't found in the swapfile, so we must read it from the elf file.

	/**
	 * We check if the virtual address provided belongs to the text segment
	*/
    if(vaddr>=as->as_vbase1 && vaddr <= as->as_vbase1 + as->as_npages1 * PAGE_SIZE ){

		DEBUG(DB_VM,"\nLOADING CODE: ");

		add_pt_type_fault(DISK);//Update statistics

		/**
		 * Some programs may have the beginning of text/data segment not aligned to a page (see testbin/bigfork as a reference). This information is lost in as_create, since
		 * as->as_vbase contains an address aligned to a page. To solve it we need to add an additional field in the as struct, offset. The offset is used when we access the
		 * first page if it's !=0. In this case, we zero-fill the page and we start loading the ELF with additional_offset as offset within the frame.
		*/
		if(as->initial_offset1!=0 && (vaddr - as->as_vbase1)==0){
			bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
			additional_offset=as->initial_offset1; //Othwerwise it's 0
			if(as->ph1.p_filesz>=PAGE_SIZE-additional_offset){
				sz=PAGE_SIZE-additional_offset; //filesz is big enough to fill the remaining part of the block, so we load PAGE_SIZE-additional_offset bytes
			}
			else{
				sz=as->ph1.p_filesz; //filesz is not enough to fill the remaining part of the block, so we load just filesz bytes.
			}
		}
		else{

			if(as->ph1.p_filesz+as->initial_offset1 - (vaddr - as->as_vbase1)<PAGE_SIZE){ //If filesz is not big enough to fill the whole page, we must zero-fill it before loading data.
				bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);//To avoid additional TLB faults, we pretend that the physical address provided belongs to the kernel. In this way, the address tranlation will just be vaddr-0x80000000.
				sz=as->ph1.p_filesz+as->initial_offset1 - (vaddr - as->as_vbase1);//We compute the file size of the last page (please notice that we must take into account as->initial_offset too).
				memsz=as->ph1.p_memsz+as->initial_offset1 - (vaddr - as->as_vbase1);//We compute the memory size of the last page (please notice that we must take into account as->initial_offset too).
			}

			if((int)(as->ph1.p_filesz+as->initial_offset1) - (int)(vaddr - as->as_vbase1)<0){//This check is fundamental to avoid issues with programs that have filesz<memsz. In fact, without this check we wouldn't zero the page, causing errors. For a deeper understanding, try to debug testbin/zero analyzing the difference between memsz and filesz.
				bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
				DEBUG(DB_VM,"LOAD ELF in 0x%x (virtual: 0x%x) for process %d\n",paddr, vaddr, pid);
				return 0;//We directly return to avoid performing a read of 0 bytes
			}
		}

		DEBUG(DB_VM,"LOAD ELF in 0x%x (virtual: 0x%x) for process %d\n",paddr, vaddr, pid);


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
			if(as->ph2.p_filesz+as->initial_offset2 - (vaddr - as->as_vbase2)<PAGE_SIZE){ 
				bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
				sz=as->ph2.p_filesz+as->initial_offset2 - (vaddr - as->as_vbase2);
				memsz=as->ph2.p_memsz+as->initial_offset2 - (vaddr - as->as_vbase2);
			}

			if((int)(as->ph2.p_filesz+as->initial_offset2) - (int)(vaddr - as->as_vbase2)<0){
				bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
				add_pt_type_fault(ELF);
				DEBUG(DB_VM,"LOAD ELF in 0x%x (virtual: 0x%x) for process %d\n",paddr, vaddr, pid);
				return 0;
			}
		}

        result = load_elf_page(as->v, as->ph2.p_offset+(vaddr - as->as_vbase2),	PADDR_TO_KVADDR(paddr+additional_offset),memsz, sz);
		if (result) {
            panic("Error while reading the data segment");
		}

		add_pt_type_fault(ELF);

		DEBUG(DB_VM,"LOAD ELF in 0x%x (virtual: 0x%x) for process %d\n",paddr, vaddr, pid);

        return 0;
    }

    /**
	 * We check if the virtual address provided belongs to the text segment.
	 * The check is performed in this way since the stack grows up from 0x80000000 (excluded) to as->as_vbase2 + as->as_npages2 * PAGE_SIZE
	*/
    if(vaddr>as->as_vbase2 + as->as_npages2 * PAGE_SIZE && vaddr<USERSTACK){

		DEBUG(DB_VM,"\nLOADING STACK: ");

		DEBUG(DB_VM,"ELF: Loading 4096 bytes to 0x%lx\n",(unsigned long) vaddr);

        //this time we just 0-fill the page, so no need to perform any kind of load.
        bzero((void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
		
		add_pt_type_fault(ZEROED);//update statistics

		DEBUG(DB_VM,"LOAD ELF in 0x%x (virtual: 0x%x) for process %d\n",paddr, vaddr, pid);

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