#include "swapfile.h"

#define MAX_SIZE 9*1024*1024

struct swapfile *swap;

int load_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    lock_acquire(swap->s_lock); //To enforce mutual exclusion, since the swapfile is shared

    DEBUG(DB_VM,"\nLOAD SWAP");

    size_t i;
    int result;
    struct iovec iov;
    struct uio ku;

    for(i=0;i<swap->size; i++){
        if(swap->elements[i].pid==pid && swap->elements[i].vaddr==vaddr){//We search for a matching entry
            add_pt_type_fault(DISK);//Update statistics

            swap->elements[i].pid=-1;//Since we move the page from swap to RAM, we mark this entry as free for the future

            DEBUG(DB_VM,"SWAP: Loading into RAM %lu bytes to 0x%lx (offset in swapfile : 0x%lx)\n",(unsigned long) PAGE_SIZE, (unsigned long) paddr, (unsigned long) i*PAGE_SIZE);
            
            uio_kinit(&iov,&ku,(void*)PADDR_TO_KVADDR(paddr),PAGE_SIZE,i*PAGE_SIZE,UIO_READ);//Again we use paddr as it was a kernel physical address to avoid a recursion of faults

            result = VOP_READ(swap->v,&ku);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }

            add_pt_type_fault(SWAPFILE);//Update statistics

            lock_release(swap->s_lock);

            return 1;//We found the entry in the swapfile, so we return 1
        }
    }

    lock_release(swap->s_lock);

    return 0;//We didn't find any entry, so we return 0
}

int store_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    lock_acquire(swap->s_lock);

    DEBUG(DB_VM,"\nSTORE SWAP");

    size_t i;
    int result;
    struct iovec iov;
    struct uio ku;

    for(i=0;i<swap->size; i++){
        if(swap->elements[i].pid==-1){//We search for a free entry

            // void *kbuf;
            //kbuf=kmalloc(PAGE_SIZE);

            DEBUG(DB_VM,"SWAP: Loading from RAM %lu bytes from 0x%lx (offset in swapfile : 0x%lx)\n",(unsigned long) PAGE_SIZE, (unsigned long) paddr, (unsigned long) i*PAGE_SIZE);

            swap->elements[i].pid=pid;
            swap->elements[i].vaddr=vaddr;//We assign the empty entry found to the page that must be stored
            
            uio_kinit(&iov,&ku,(void*)PADDR_TO_KVADDR(paddr),PAGE_SIZE,i*PAGE_SIZE,UIO_WRITE);

            result = VOP_WRITE(swap->v,&ku);//We write on the swapfile
            if(result){
                panic("VOP_WRITE in swapfile failed, with result=%d",result);
            }

            //kfree(kbuf);

            add_swap_writes();//Update statistics
            
            lock_release(swap->s_lock);

            return 1;
        }
    }

    panic("The swapfile is full!");//If we didn't find any free entry the swapfile was full, and we panic
}

int swap_init(void){
    int result;
    size_t i;
    char fname[9];

    strcpy(fname,"lhd0raw:");//As a swapfile, we use lhd0raw:

    swap = kmalloc(sizeof(struct swapfile));
    if(!swap){
        panic("Error during swap allocation");
    }

    result = vfs_open(fname, O_RDWR , 0, &swap->v);//We open the swapfile
    if (result) {
		return result;
	}

    swap->s_lock = lock_create("swap_lock");

    swap->size = MAX_SIZE/PAGE_SIZE;//Number of pages in our swapfile
    swap->elements = kmalloc(swap->size*sizeof(struct swap_cell));
    if(!swap->elements){
        panic("Error during swap elements allocation");
    }

    for(i=0;i<swap->size; i++){
        swap->elements[i].pid=-1;//We mark all the pages of the swapfile as free
    }

    return 0;
    
}

void remove_process_from_swap(pid_t pid){
    size_t i;

    lock_acquire(swap->s_lock);

    for(i=0;i<swap->size;i++){
        if(swap->elements[i].pid==pid){//If a page belongs to the ended process, we mark it as free
            swap->elements[i].pid=-1;
        }
    }

    lock_release(swap->s_lock);

}