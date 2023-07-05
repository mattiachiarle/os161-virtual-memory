#include "swapfile.h"

#define MAX_SIZE 9*1024*1024

struct swapfile *swap;

int load_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    size_t i;
    int result;
    void *kbuf;
    struct iovec iov;
    struct uio ku;

    for(i=0;i<swap->size; i++){
        if(swap->elements[i].pid==pid && swap->elements[i].vaddr==vaddr){
            swap->elements[i].pid=-1;
            kbuf=kmalloc(PAGE_SIZE);
            
            uio_kinit(&iov,&ku,kbuf,PAGE_SIZE,i*PAGE_SIZE,UIO_READ);

            result = VOP_READ(swap->v,&ku);
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }

            copyout(kbuf, (userptr_t)paddr, PAGE_SIZE);
            kfree(kbuf);

            return 1;
        }
    }

    return 0;
}

int store_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    size_t i;
    int result;
    void *kbuf;
    struct iovec iov;
    struct uio ku;

    for(i=0;i<swap->size; i++){
        if(swap->elements[i].pid==-1){
            swap->elements[i].pid=pid;
            swap->elements[i].vaddr=vaddr;
            kbuf=kmalloc(PAGE_SIZE);

            copyin((userptr_t)paddr,kbuf,PAGE_SIZE);
            
            uio_kinit(&iov,&ku,kbuf,PAGE_SIZE,i*PAGE_SIZE,UIO_WRITE);

            result = VOP_WRITE(swap->v,&ku);
            if(result){
                panic("VOP_WRITE in swapfile failed, with result=%d",result);
            }

            kfree(kbuf);

            return 1;
        }
    }

    panic("The swapfile is full!");
}

int swap_init(void){
    int result;
    size_t i;
    char fname[9];

    strcpy(fname,"lhd0raw:");

    swap = kmalloc(sizeof(struct swapfile));
    if(!swap){
        panic("Error during swap allocation");
    }

    result = vfs_open( fname, O_RDWR , 0, &swap->v);
    if (result) {
		return result;
	}

    swap->size = MAX_SIZE/PAGE_SIZE;
    swap->elements = kmalloc(swap->size*sizeof(struct swap_cell));
    if(!swap->elements){
        panic("Error during swap elements allocation");
    }

    for(i=0;i<swap->size; i++){
        swap->elements[i].pid=-1;
    }

    return 0;
    
}

void remove_process_from_swap(pid_t pid){
    size_t i;

    for(i=0;i<swap->size;i++){
        if(swap->elements[i].pid==pid){
            swap->elements[i].pid=-1;
        }
    }
    
}