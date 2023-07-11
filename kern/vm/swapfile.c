#include "swapfile.h"

#define MAX_SIZE 9*1024*1024

//static int occ = 0;

struct swapfile *swap;

/**
 * Debugging function. Given a pid, it prints text, data and stack lists for that proces.
 * 
 * @param pid: pid of the process.
*/
#if OPT_DEBUG
static void print_list(pid_t pid){

    struct swap_cell *i;

    kprintf("Text list:\n");
    for(i=swap->text[pid];i!=NULL;i=i->next){
        kprintf("addr: 0x%x, offset: 0x%x, next: 0x%x\n",i->vaddr,i->offset,(unsigned int)i->next);
    }
    kprintf("Data list:\n");
    for(i=swap->data[pid];i!=NULL;i=i->next){
        kprintf("addr: 0x%x, offset: 0x%x, next: 0x%x\n",i->vaddr,i->offset,(unsigned int)i->next);
    }
    kprintf("Stack list:\n");
    for(i=swap->stack[pid];i!=NULL;i=i->next){
        kprintf("addr: 0x%x, offset: 0x%x, next: 0x%x\n",i->vaddr,i->offset,(unsigned int)i->next);
    }
    kprintf("\n");

}
#endif

int load_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    //lock_acquire(swap->s_lock); //To enforce mutual exclusion, since the swapfile is shared

    int result;
    struct iovec iov;
    struct uio ku;

    #if OPT_SW_LIST
    struct addrspace *as=proc_getas();
    struct swap_cell *list=NULL, *prev=NULL;
    int seg=-1;

    if(vaddr>=as->as_vbase1 && vaddr <= as->as_vbase1 + as->as_npages1 * PAGE_SIZE ){
        list = swap->text[pid];
        seg=0;
    }

    if(vaddr>=as->as_vbase2 && vaddr <= as->as_vbase2 + as->as_npages2 * PAGE_SIZE ){
        list = swap->data[pid];
        seg=1;
    }

    if(vaddr <= USERSTACK && seg==-1){
        list = swap->stack[pid];
        seg=2;
    }

    DEBUG(DB_VM,"\nLOAD SWAP in 0x%x\n",list->offset);
    
    while(list!=NULL){
        if(list->vaddr==vaddr){
            add_pt_type_fault(DISK);//Update statistics

            uio_kinit(&iov,&ku,(void*)PADDR_TO_KVADDR(paddr),PAGE_SIZE,list->offset,UIO_READ);//Again we use paddr as it was a kernel physical address to avoid a recursion of faults

            //lock_acquire(swap->s_lock);
            result = VOP_READ(swap->v,&ku);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }
            //lock_release(swap->s_lock);

            add_pt_type_fault(SWAPFILE);//Update statistics

            if(prev!=NULL){
                prev->next=list->next;
                list->next=swap->free;
                swap->free=list;
            }
            else{
                if(seg==0){
                    swap->text[pid]=list->next;
                }
                if(seg==1){
                    swap->data[pid]=list->next;
                }
                if(seg==2){
                    swap->stack[pid]=list->next;
                }
                list->next=swap->free;
                swap->free=list;
            }

            //lock_release(swap->s_lock);

            //print_list(pid);

            return 1;//We found the entry in the swapfile, so we return 1
        }

        prev=list;
        list=list->next;
    }

    #else
    int i;
    for(i=0;i<swap->size; i++){
        if(swap->elements[i].pid==pid && swap->elements[i].vaddr==vaddr){//We search for a matching entry
            add_pt_type_fault(DISK);//Update statistics

            swap->elements[i].pid=-1;//Since we move the page from swap to RAM, we mark this entry as free for the future

            //DEBUG(DB_VM,
            //kprintf("SWAP: Process %d loading into RAM %lu bytes to 0x%lx (offset in swapfile : 0x%lx)\n",curproc->p_pid,(unsigned long) PAGE_SIZE, (unsigned long) paddr, (unsigned long) i*PAGE_SIZE);
            
            uio_kinit(&iov,&ku,(void*)PADDR_TO_KVADDR(paddr),PAGE_SIZE,i*PAGE_SIZE,UIO_READ);//Again we use paddr as it was a kernel physical address to avoid a recursion of faults

            //lock_acquire(swap->s_lock);
            result = VOP_READ(swap->v,&ku);//We perform the read
            //lock_release(swap->s_lock);
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }

            add_pt_type_fault(SWAPFILE);//Update statistics

            occ--;

            //kprintf("Process %d read. Now occ=%d\n",curproc->p_pid,occ);

            //lock_release(swap->s_lock);

            return 1;//We found the entry in the swapfile, so we return 1
        }
    }
    #endif

    //lock_release(swap->s_lock);

    return 0;//We didn't find any entry, so we return 0
}

int store_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    //lock_acquire(swap->s_lock);

    int result;
    struct iovec iov;
    struct uio ku;

    #if OPT_SW_LIST

    struct addrspace *as=proc_getas();
    struct swap_cell *free_frame;
    int flag=0;

    free_frame=swap->free;

    if(free_frame==NULL){
        panic("The swapfile is full!");//If we didn't find any free entry the swapfile was full, and we panic
    }

    swap->free = free_frame->next;

    if(vaddr>=as->as_vbase1 && vaddr <= as->as_vbase1 + as->as_npages1 * PAGE_SIZE ){
        free_frame->next=swap->text[pid];
        free_frame->vaddr=vaddr;
        swap->text[pid]=free_frame;
        flag=1;
    }

    if(vaddr>=as->as_vbase2 && vaddr <= as->as_vbase2 + as->as_npages2 * PAGE_SIZE ){
        free_frame->next=swap->data[pid];
        free_frame->vaddr=vaddr;
        swap->data[pid]=free_frame;
        flag=1;
    }

    if(vaddr <= USERSTACK && !flag ){
        free_frame->next=swap->stack[pid];
        free_frame->vaddr=vaddr;
        swap->stack[pid]=free_frame;
    }

    //print_list(pid);

    DEBUG(DB_VM,"STORE SWAP in 0x%x\n",free_frame->offset);

    uio_kinit(&iov,&ku,(void*)PADDR_TO_KVADDR(paddr),PAGE_SIZE,free_frame->offset,UIO_WRITE);

    //lock_acquire(swap->s_lock);
    result = VOP_WRITE(swap->v,&ku);//We write on the swapfile
    if(result){
        panic("VOP_WRITE in swapfile failed, with result=%d",result);
    }
    //lock_release(swap->s_lock);

    add_swap_writes();//Update statistics
            
    //lock_release(swap->s_lock);

    return 1;

    #else
    int i;
    for(i=0;i<swap->size; i++){
        if(swap->elements[i].pid==-1){//We search for a free entry

            // void *kbuf;
            //kbuf=kmalloc(PAGE_SIZE);

            DEBUG(DB_VM,"SWAP: Loading from RAM %lu bytes from 0x%lx (offset in swapfile : 0x%lx)\n",(unsigned long) PAGE_SIZE, (unsigned long) paddr, (unsigned long) i*PAGE_SIZE);

            swap->elements[i].pid=pid;
            swap->elements[i].vaddr=vaddr;//We assign the empty entry found to the page that must be stored
            
            uio_kinit(&iov,&ku,(void*)PADDR_TO_KVADDR(paddr),PAGE_SIZE,i*PAGE_SIZE,UIO_WRITE);

            //lock_acquire(swap->s_lock);
            result = VOP_WRITE(swap->v,&ku);//We write on the swapfile
            //lock_release(swap->s_lock);
            if(result){
                panic("VOP_WRITE in swapfile failed, with result=%d",result);
            }

            //kfree(kbuf);

            add_swap_writes();//Update statistics

            occ++;

            //kprintf("Process %d wrote. Now occ=%d\n",curproc->p_pid,occ);
            
            //lock_release(swap->s_lock);

            return 1;
        }
    }
    #endif

    panic("The swapfile is full!");//If we didn't find any free entry the swapfile was full, and we panic
}

int swap_init(void){
    int result;
    int i;
    char fname[9];
    #if OPT_SW_LIST
    struct swap_cell *tmp;
    #endif

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

    #if OPT_SW_LIST
    swap->text = kmalloc(MAX_PROC*sizeof(struct swap_cell *));
    if(!swap->text){
        panic("Error during text elements allocation");
    }

    swap->data = kmalloc(MAX_PROC*sizeof(struct swap_cell *));
    if(!swap->data){
        panic("Error during data elements allocation");
    }

    swap->stack = kmalloc(MAX_PROC*sizeof(struct swap_cell *));
    if(!swap->stack){
        panic("Error during stack elements allocation");
    }

    swap->free=NULL;
    #else
    swap->elements = kmalloc(swap->size*sizeof(struct swap_cell));

    if(!swap->elements){
        panic("Error during swap elements allocation");
    }
    #endif

    #if OPT_SW_LIST
    for(i=0;i<MAX_PROC;i++){
        swap->text[i]=NULL;
        swap->data[i]=NULL;
        swap->stack[i]=NULL;
    }
    #endif

    for(i=(int)(swap->size-1); i>=0; i--){//We create all the elements in the free list
        #if OPT_SW_LIST
        tmp=kmalloc(sizeof(struct swap_cell));
        if(!tmp){
            panic("Error during swap elements allocation");
        }
        tmp->offset=i*PAGE_SIZE;
        tmp->next=swap->free;
        swap->free=tmp;
        #else
        swap->elements[i].pid=-1;//We mark all the pages of the swapfile as free
        #endif
    }

    return 0;
    
}

void remove_process_from_swap(pid_t pid){

    //kprintf("Process %d is freeing!\n",pid);

    //lock_acquire(swap->s_lock);

    #if OPT_SW_LIST
    struct swap_cell *elem, *next;

    if(swap->text[pid]!=NULL){
        for(elem=swap->text[pid];elem!=NULL;elem=next){
            next=elem->next;
            elem->next=swap->free;
            swap->free=elem;
        }
        swap->text[pid]=NULL;
    }

    if(swap->data[pid]!=NULL){
        for(elem=swap->data[pid];elem!=NULL;elem=next){
            next=elem->next;
            elem->next=swap->free;
            swap->free=elem;
        }
        swap->data[pid]=NULL;
    }

    if(swap->stack[pid]!=NULL){
        for(elem=swap->stack[pid];elem!=NULL;elem=next){
             next=elem->next;
            elem->next=swap->free;
            swap->free=elem;
        }
        swap->stack[pid]=NULL;
    }

    //print_list(pid);

    #else
    int i;
    
    for(i=0;i<swap->size;i++){
        if(swap->elements[i].pid==pid){//If a page belongs to the ended process, we mark it as free
            occ--;
            //kprintf("Process %d released. Now occ=%d\n",curproc->p_pid,occ);
            swap->elements[i].pid=-1;
        }
    }
    #endif

    //lock_release(swap->s_lock);

}

void copy_swap_pages(pid_t old_pid, pid_t new_pid){

    // lock_acquire(swap->s_lock);
    // P(peps.sem);

    void *kbuf = kmalloc(PAGE_SIZE);
    struct uio u;
    struct iovec iov;
    int result;

    #if OPT_SW_LIST
    struct swap_cell *ptr, *free;
    
    if(swap->text[old_pid]!=NULL){
        for(ptr = swap->text[old_pid]; ptr!=NULL; ptr=ptr->next){
            free=swap->free;

            if(free==NULL){
                panic("The swapfile is full!");//We don't have enough pages to perform the fork
            }

            swap->free = free->next;
            free->next = swap->text[new_pid];
            swap->text[new_pid] = free;
            free->vaddr = ptr->vaddr;
            //kprintf("Copying from 0x%x to 0x%x",ptr->offset,free->offset);
            uio_kinit(&iov,&u,kbuf,PAGE_SIZE,ptr->offset,UIO_READ);
            //lock_acquire(swap->s_lock);
            result = VOP_READ(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }


            // for(int i=0;i<PAGE_SIZE;i++){
            //     kprintf("%c",kbuf[i]);
            // }

            uio_kinit(&iov,&u,(void* )kbuf,PAGE_SIZE,free->offset,UIO_WRITE);
            result = VOP_WRITE(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }
            //lock_release(swap->s_lock);
        }
    }
    if(swap->data[old_pid]!=NULL){
        for(ptr = swap->data[old_pid]; ptr!=NULL; ptr=ptr->next){
            free=swap->free;

            if(free==NULL){
                panic("The swapfile is full!");//We don't have enough pages to perform the fork
            }

            swap->free = free->next;
            free->next = swap->data[new_pid];
            swap->data[new_pid] = free;
            free->vaddr = ptr->vaddr;
            //kprintf("Copying from 0x%x to 0x%x",ptr->offset,free->offset);
            uio_kinit(&iov,&u,kbuf,PAGE_SIZE,ptr->offset,UIO_READ);
            //lock_acquire(swap->s_lock);
            result = VOP_READ(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }

            uio_kinit(&iov,&u,kbuf,PAGE_SIZE,free->offset,UIO_WRITE);
            result = VOP_WRITE(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }
            //lock_release(swap->s_lock);

        }
    }

    if(swap->stack[old_pid]!=NULL){
        for(ptr = swap->stack[old_pid]; ptr!=NULL; ptr=ptr->next){
            free=swap->free;

            if(free==NULL){
                panic("The swapfile is full!");//We don't have enough pages to perform the fork
            }

            swap->free = free->next;
            free->next = swap->stack[new_pid];
            swap->stack[new_pid] = free;
            free->vaddr = ptr->vaddr;
            //kprintf("Copying from 0x%x to 0x%x",ptr->offset,free->offset);
            uio_kinit(&iov,&u,kbuf,PAGE_SIZE,ptr->offset,UIO_READ);
            //lock_acquire(swap->s_lock);
            result = VOP_READ(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }

            uio_kinit(&iov,&u,kbuf,PAGE_SIZE,free->offset,UIO_WRITE);
            result = VOP_WRITE(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }
            //lock_release(swap->s_lock);
        }
    }

    #else
    int i,j;

    for(i=0;i<swap->size;i++){
        if(swap->elements[i].pid==old_pid){
            for(j=0;j<swap->size; j++){
                if(swap->elements[j].pid==-1){//We search for a free entry

                    swap->elements[j].pid=new_pid;
                    swap->elements[j].vaddr=swap->elements[i].vaddr;//We assign the empty entry found to the page that must be stored
                    
                    uio_kinit(&iov,&u,(void* )kbuf,PAGE_SIZE,i*PAGE_SIZE,UIO_READ);
                    result = VOP_READ(swap->v,&u);//We perform the read
                    if(result){
                        panic("VOP_READ in swapfile failed, with result=%d",result);
                    }

                    for(int k=0;k<PAGE_SIZE;k++){
                        kprintf("%c",kbuf[i]);
                    }

                    uio_kinit(&iov,&u,(void* )kbuf,PAGE_SIZE,j*PAGE_SIZE,UIO_WRITE);
                    result = VOP_WRITE(swap->v,&u);//We perform the read
                    if(result){
                        panic("VOP_READ in swapfile failed, with result=%d",result);
                    }
                    
                    break;
                }
            }
            if(j==swap->size){
                panic("The swapfile is full!");//We don't have enough pages to perform the fork
            }
            occ++;

            //kprintf("Process %d copied a page into %d. Now occ=%d\n",old_pid, new_pid, occ);
        }
    }

    #endif

    kfree(kbuf);

    // lock_release(swap->s_lock);
    // V(peps.sem);
}