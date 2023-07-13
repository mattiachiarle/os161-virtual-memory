#include "swapfile.h"

#define MAX_SIZE 9*1024*1024

#if !OPT_SW_LIST
static int occ = 0;
#endif

struct swapfile *swap;

/**
 * Debugging function. Given a pid, it prints text, data and stack lists for that proces.
 * 
 * @param pid: pid of the process.
*/
//#if OPT_DEBUG
void print_list(pid_t pid){

    struct swap_cell *i;

    kprintf("SWAP LIST FOR PROCESS %d:\n",pid);
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
//#endif

int load_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr, int spl){

    //lock_acquire(swap->s_lock); //To enforce mutual exclusion, since the swapfile is shared
    // P(peps.sem);
    // P(swap->s_sem);

    int result;
    struct iovec iov;
    struct uio ku;

    KASSERT(pid==curproc->p_pid);

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

    if(vaddr <= USERSTACK && vaddr>as->as_vbase2 + as->as_npages2 * PAGE_SIZE){
        list = swap->stack[pid];
        seg=2;
    }

    if(seg==-1){
        panic("Wrong vaddr for load: 0x%x, process=%d\n",vaddr,curproc->p_pid);
    }

    
    while(list!=NULL){
        if(list->vaddr==vaddr){

            KASSERT(!list->load);
            KASSERT(!list->swap);
            list->vaddr=1;
            list->load=1;

            if(prev!=NULL){
                KASSERT(prev->next==list);
                // kprintf("We removed 0x%x from process %d, so now 0x%x points to 0x%x\n",vaddr,pid,prev!=NULL?prev->vaddr:(unsigned int)NULL,prev->next->vaddr);
                prev->next=list->next;
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
            }

            lock_acquire(list->cell_lock);
            while(list->store){
                splx(spl);
                cv_wait(list->cell_cv,list->cell_lock);
                spl = splhigh();
            }
            lock_release(list->cell_lock);
            
            //DEBUG(DB_VM,
            // kprintf("LOAD SWAP in 0x%x (virtual: 0x%x) for process %d\n",list->offset, vaddr, pid);

            add_pt_type_fault(DISK);//Update statistics

            uio_kinit(&iov,&ku,(void*)PADDR_TO_KVADDR(paddr),PAGE_SIZE,list->offset,UIO_READ);//Again we use paddr as it was a kernel physical address to avoid a recursion of faults

            // lock_acquire(swap->s_lock);
            // P(swap->s_sem);
            result = VOP_READ(swap->v,&ku);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }
            P(swap->s_sem);
            list->load=0;
            // kprintf("ENDED LOAD SWAP in 0x%x (virtual: 0x%x) for process %d\n",list->offset, list->vaddr, pid);
            // lock_release(swap->s_lock);
            // V(swap->s_sem);

            list->next=swap->free;
            swap->free=list;

            add_pt_type_fault(SWAPFILE);//Update statistics

            list->vaddr=0;

            // print_list(pid);
            V(swap->s_sem);

            //lock_release(swap->s_lock);

            //print_list(pid);

            KASSERT(swap->free->load==0);

            return 1;//We found the entry in the swapfile, so we return 1
        }
        P(swap->s_sem);
        prev=list;
        list=list->next;
        KASSERT(prev->next==list);
        V(swap->s_sem);
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
    // V(peps.sem);
    // V(swap->s_sem);

    //lock_release(swap->s_lock);

    return 0;//We didn't find any entry, so we return 0
}

int store_swap(vaddr_t vaddr, pid_t pid, paddr_t paddr){

    // P(peps.sem);

    //lock_acquire(swap->s_lock);

    //KASSERT(pid==curproc->p_pid);

    int result;
    struct iovec iov;
    struct uio ku;

    P(swap->s_sem);

    #if OPT_SW_LIST

    struct addrspace *as=proc_getas();
    struct swap_cell *free_frame;
    int flag=0;

    // P(swap->s_sem);

    free_frame=swap->free;

    // while(free_frame->store || free_frame->load || free_frame->swap){
    //     free_frame=free_frame->next;
    // }

    if(free_frame==NULL){
        panic("The swapfile is full!");//If we didn't find any free entry the swapfile was full, and we panic
    }

    KASSERT(free_frame->store==0);
    KASSERT(free_frame->load==0);
    KASSERT(free_frame->swap==0);

    swap->free = free_frame->next;

    if(vaddr>=as->as_vbase1 && vaddr <= as->as_vbase1 + as->as_npages1 * PAGE_SIZE ){
        free_frame->next=swap->text[pid];
        swap->text[pid]=free_frame;
        // free_frame->vaddr=1;
        flag=1;
    }

    if(vaddr>=as->as_vbase2 && vaddr <= as->as_vbase2 + as->as_npages2 * PAGE_SIZE ){
        free_frame->next=swap->data[pid];
        swap->data[pid]=free_frame;
        // free_frame->vaddr=1;
        flag=1;
    }

    if(vaddr <= USERSTACK && vaddr>as->as_vbase2 + as->as_npages2 * PAGE_SIZE){
        free_frame->next=swap->stack[pid];
        swap->stack[pid]=free_frame;
        // free_frame->vaddr=1;
        flag=1;
    }

    if(flag==0){
        panic("Wrong vaddr for store: 0x%x\n",vaddr);
    }

    free_frame->vaddr=vaddr;

    //print_list(pid);

    //DEBUG(DB_VM,
    // kprintf("STORE SWAP in 0x%x (virtual: 0x%x) for process %d\n",free_frame->offset, free_frame->vaddr, pid);

    free_frame->store=1;

    V(swap->s_sem);

    uio_kinit(&iov,&ku,(void*)PADDR_TO_KVADDR(paddr),PAGE_SIZE,free_frame->offset,UIO_WRITE);

    // lock_acquire(swap->s_lock);
    
    result = VOP_WRITE(swap->v,&ku);//We write on the swapfile
    if(result){
        panic("VOP_WRITE in swapfile failed, with result=%d",result);
    }
    // free_frame->vaddr=vaddr;
    free_frame->store=0;
    lock_acquire(free_frame->cell_lock);
    cv_broadcast(free_frame->cell_cv, free_frame->cell_lock);
    lock_release(free_frame->cell_lock);
    // kprintf("ENDED STORE SWAP in 0x%x (virtual: 0x%x) for process %d\n",free_frame->offset, free_frame->vaddr, pid);
    // lock_release(swap->s_lock);
    // V(swap->s_sem);

    // kprintf("We added 0x%x to process %d, that points to 0x%x\n",vaddr,pid,free_frame->next?free_frame->next->vaddr:0x0);

    add_swap_writes();//Update statistics

    // print_list(pid);
            
    //lock_release(swap->s_lock);
    // V(peps.sem);

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
    swap->s_sem = sem_create("swap_sem",1);

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

    swap->start_text = kmalloc(MAX_PROC*sizeof(struct swap_cell *));
    if(!swap->start_text){
        panic("Error during swap text elements allocation");
    }

    swap->start_data = kmalloc(MAX_PROC*sizeof(struct swap_cell *));
    if(!swap->start_data){
        panic("Error during swap data elements allocation");
    }

    swap->start_stack = kmalloc(MAX_PROC*sizeof(struct swap_cell *));
    if(!swap->start_stack){
        panic("Error during swap stack elements allocation");
    }

    // swap->ntext_swap = kmalloc(MAX_PROC*sizeof(int));
    // if(!swap->ntext_swap){
    //     panic("Error during stack elements allocation");
    // }

    // swap->ndata_swap = kmalloc(MAX_PROC*sizeof(int));
    // if(!swap->ntext_swap){
    //     panic("Error during stack elements allocation");
    // }

    // swap->nstack_swap = kmalloc(MAX_PROC*sizeof(int));
    // if(!swap->ntext_swap){
    //     panic("Error during stack elements allocation");
    // }

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
        swap->start_text[i]=NULL;
        swap->start_data[i]=NULL;
        swap->start_stack[i]=NULL;
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
        tmp->load=0;
        tmp->store=0;
        tmp->swap=0;
        tmp->cell_cv = cv_create("cell_cv");
        tmp->cell_lock = lock_create("cell_lock");
        swap->free=tmp;
        #else
        swap->elements[i].pid=-1;//We mark all the pages of the swapfile as free
        #endif
    }

    return 0;
    
}

static int r=0;

void remove_process_from_swap(pid_t pid, int spl){

    //kprintf("Process %d is freeing!\n",pid);

    //lock_acquire(swap->s_lock);

    P(swap->s_sem);

    #if OPT_SW_LIST
    struct swap_cell *elem, *next;

    if(swap->text[pid]!=NULL){
        if(r==0){
            // kprintf("FIRST REMOVE PROCESS FROM SWAP\n");
            r++;
        }
        for(elem=swap->text[pid];elem!=NULL;elem=next){
            V(swap->s_sem);
            lock_acquire(elem->cell_lock);
            while(elem->store){
                splx(spl);
                cv_wait(elem->cell_cv,elem->cell_lock);
                spl = splhigh();
            }
            lock_release(elem->cell_lock);
            P(swap->s_sem);
            if(elem->load || elem->swap){
                // kprintf("Error with swap freeing text for process %d, paddr 0x%x, vaddr 0x%x, active=%d, swap=%d\n",curproc->p_pid,elem->offset,elem->vaddr,elem->active,elem->swap);
            }
            KASSERT(!elem->load && !elem->swap && !elem->store);
            next=elem->next;
            elem->next=swap->free;
            swap->free=elem;
        }
        swap->text[pid]=NULL;
    }

    if(swap->data[pid]!=NULL){
        if(r==0){
            // kprintf("FIRST REMOVE PROCESS FROM SWAP\n");
            r++;
        }
        for(elem=swap->data[pid];elem!=NULL;elem=next){
            V(swap->s_sem);
            lock_acquire(elem->cell_lock);
            while(elem->store){
                splx(spl);
                cv_wait(elem->cell_cv,elem->cell_lock);
                spl = splhigh();
            }
            lock_release(elem->cell_lock);
            P(swap->s_sem);
            if(elem->load || elem->swap){
                // kprintf("Error with swap freeing data for process %d, paddr 0x%x, vaddr 0x%x, active=%d, swap=%d\n",curproc->p_pid,elem->offset,elem->vaddr,elem->active,elem->swap);
            }
            KASSERT(!elem->load && !elem->swap && !elem->store);
            next=elem->next;
            elem->next=swap->free;
            swap->free=elem;
        }
        swap->data[pid]=NULL;
    }

    if(swap->stack[pid]!=NULL){
        if(r==0){
            // kprintf("FIRST REMOVE PROCESS FROM SWAP\n");
            r++;
        }
        for(elem=swap->stack[pid];elem!=NULL;elem=next){
            V(swap->s_sem);
            lock_acquire(elem->cell_lock);
            while(elem->store){
                splx(spl);
                cv_wait(elem->cell_cv,elem->cell_lock);
                spl = splhigh();
            }
            lock_release(elem->cell_lock);
            P(swap->s_sem);
            if(elem->load || elem->swap){
                // kprintf("Error with swap freeing stack for process %d, paddr 0x%x, vaddr 0x%x, active=%d, swap=%d\n",curproc->p_pid,elem->offset,elem->vaddr,elem->active,elem->swap);
            }
            KASSERT(!elem->load && !elem->swap && !elem->store);
            next=elem->next;
            elem->next=swap->free;
            swap->free=elem;
        }
        swap->stack[pid]=NULL;
    }

    V(swap->s_sem);

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

static int n=0;

void copy_swap_pages(pid_t new_pid, pid_t old_pid, int spl){

    // lock_acquire(swap->s_lock);
    // P(peps.sem);

    //kprintf("Process %d performs a kmalloc to fork %d\n",curproc->p_pid,new_pid);
    struct uio u;
    struct iovec iov;
    int result;
    // int flag=0;

    KASSERT(swap->start_text[new_pid]==swap->text[old_pid]);
    KASSERT(swap->start_data[new_pid]==swap->data[old_pid]);
    KASSERT(swap->start_stack[new_pid]==swap->stack[old_pid]);

    #if OPT_SW_LIST
    // struct swap_cell *start_text, *start_data, *start_stack, *free;

    // start_text=swap->text[old_pid];
    // start_data=swap->data[old_pid];
    // start_stack=swap->stack[old_pid];

    struct swap_cell *ptr, *free;
    
    if(swap->start_text[new_pid]!=NULL){
        void *kbuf = kmalloc(PAGE_SIZE);
        if(n==0){
            // kprintf("FIRST SWAP COPY FOR FORK\n");
            n++;
        }
        // flag=1;
        for(ptr = swap->start_text[new_pid], free = swap->text[new_pid]; ptr!=NULL; ptr=ptr->next, free=free->next){
            KASSERT(ptr->swap==1);
            P(swap->s_sem);
            // free=swap->free;

            if(free==NULL){
                panic("The swapfile is full!");//We don't have enough pages to perform the fork
            }

            // while(free->store || free->load || free->swap){
            //     free=free->next;
            // }

            KASSERT(!free->load);
            KASSERT(!free->store);
            KASSERT(!free->swap);

            // swap->free = free->next;
            // free->next = swap->text[new_pid];
            // swap->text[new_pid] = free;
            free->swap=1;
            // KASSERT(ptr->vaddr!=1);
            KASSERT(free->vaddr!=1);
            KASSERT(!ptr->load);
            V(swap->s_sem);
            lock_acquire(ptr->cell_lock);
            while(ptr->store){
                splx(spl);
                cv_wait(ptr->cell_cv,ptr->cell_lock);
                spl = splhigh();
            }
            lock_release(ptr->cell_lock);
            // kprintf("Copying from 0x%x to 0x%x\n",ptr->offset,free->offset);
            uio_kinit(&iov,&u,kbuf,PAGE_SIZE,ptr->offset,UIO_READ);
            // lock_acquire(swap->s_lock);
            // P(swap->s_sem);
            result = VOP_READ(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }

            // for(int i=0;i<PAGE_SIZE;i++){
            //     kprintf("%c",kbuf[i]);
            // }

            uio_kinit(&iov,&u,kbuf,PAGE_SIZE,free->offset,UIO_WRITE);
            result = VOP_WRITE(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }
            // kprintf("Copied text from 0x%x to 0x%x for process %d\n",ptr->vaddr,free->vaddr,new_pid);
            free->swap=0;
            free->vaddr = ptr->vaddr;
            KASSERT(ptr->vaddr!=1);
            KASSERT(free->vaddr!=1);
            // ptr->active=0;
            // lock_release(swap->s_lock);
            // V(swap->s_sem);
        }
        KASSERT(free==NULL);
        kfree(kbuf);
    }
    if(swap->start_data[new_pid]!=NULL){
        if(n==0){
            // kprintf("FIRST SWAP COPY FOR FORK\n");
            n++;
        }
        void *kbuf = kmalloc(PAGE_SIZE);
        // flag=1;
        for(ptr = swap->start_data[new_pid], free = swap->data[new_pid]; ptr!=NULL; ptr=ptr->next, free=free->next){
            KASSERT(ptr->swap==1);
            P(swap->s_sem);
            // free=swap->free;

            if(free==NULL){
                panic("The swapfile is full!");//We don't have enough pages to perform the fork
            }

            // while(free->store || free->load || free->swap){
            //     free=free->next;
            // }

            KASSERT(!free->load);
            KASSERT(!free->store);
            KASSERT(!free->swap);

            // swap->free = free->next;
            // free->next = swap->data[new_pid];
            // swap->data[new_pid] = free;
            free->swap=1;
            // KASSERT(ptr->vaddr!=1);
            KASSERT(free->vaddr!=1);
            KASSERT(!ptr->load);
            V(swap->s_sem);
            lock_acquire(ptr->cell_lock);
            while(ptr->store){
                splx(spl);
                cv_wait(ptr->cell_cv,ptr->cell_lock);
                spl = splhigh();
            }
            lock_release(ptr->cell_lock);
            // kprintf("Copying from 0x%x to 0x%x\n",ptr->offset,free->offset);
            uio_kinit(&iov,&u,kbuf,PAGE_SIZE,ptr->offset,UIO_READ);
            // lock_acquire(swap->s_lock);
            // P(swap->s_sem);
            result = VOP_READ(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }

            uio_kinit(&iov,&u,kbuf,PAGE_SIZE,free->offset,UIO_WRITE);
            result = VOP_WRITE(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }
            // kprintf("Copied data from 0x%x to 0x%x for process %d\n",ptr->vaddr,free->vaddr,new_pid);
            free->swap=0;
            free->vaddr = ptr->vaddr;
            KASSERT(ptr->vaddr!=1);
            KASSERT(free->vaddr!=1);
            // ptr->active=0;
            // lock_release(swap->s_lock);
            // V(swap->s_sem);
        }
        KASSERT(free==NULL);
        kfree(kbuf);
    }

    if(swap->start_stack[new_pid]!=NULL){
        if(n==0){
            // kprintf("FIRST SWAP COPY FOR FORK\n");
            n++;
        }
        void *kbuf = kmalloc(PAGE_SIZE);
        // flag=1;
        for(ptr = swap->start_stack[new_pid], free = swap->stack[new_pid]; ptr!=NULL; ptr=ptr->next, free=free->next){
            KASSERT(ptr->swap==1);
            P(swap->s_sem);
            // free=swap->free;

            if(free==NULL){
                panic("The swapfile is full!");//We don't have enough pages to perform the fork
            }

            // while(free->store || free->load || free->swap){
            //     free=free->next;
            // }

            KASSERT(!free->load);
            KASSERT(!free->store);
            KASSERT(!free->swap);

            // swap->free = free->next;
            // free->next = swap->stack[new_pid];
            // swap->stack[new_pid] = free;
            free->swap=1;
            // KASSERT(ptr->vaddr!=1);
            KASSERT(free->vaddr!=1);
            KASSERT(!ptr->load);
            V(swap->s_sem);
            lock_acquire(ptr->cell_lock);
            while(ptr->store){
                splx(spl);
                cv_wait(ptr->cell_cv,ptr->cell_lock);
                spl = splhigh();
            }
            lock_release(ptr->cell_lock);
            // kprintf("Copying from 0x%x to 0x%x\n",ptr->offset,free->offset);
            uio_kinit(&iov,&u,kbuf,PAGE_SIZE,ptr->offset,UIO_READ);
            // lock_acquire(swap->s_lock);
            // P(swap->s_sem);
            result = VOP_READ(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }

            uio_kinit(&iov,&u,kbuf,PAGE_SIZE,free->offset,UIO_WRITE);
            result = VOP_WRITE(swap->v,&u);//We perform the read
            if(result){
                panic("VOP_READ in swapfile failed, with result=%d",result);
            }
            // kprintf("Copied stack from 0x%x to 0x%x for process %d\n",ptr->vaddr,free->vaddr,new_pid);
            free->swap=0;
            free->vaddr = ptr->vaddr;
            KASSERT(ptr->vaddr!=1);
            KASSERT(free->vaddr!=1);
            // ptr->active=0;
            // lock_release(swap->s_lock);
            // V(swap->s_sem);
        }
        KASSERT(free==NULL);
        kfree(kbuf);
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

                    // for(int k=0;k<PAGE_SIZE;k++){
                    //     kprintf("%c",kbuf[i]);
                    // }

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

    // if(flag)
        // kprintf("Completed the swap copy from %d to %d\n",curproc->p_pid,new_pid);

    // lock_release(swap->s_lock);
    // V(peps.sem);
}

void prepare_copy_swap(pid_t old, pid_t new){
    struct swap_cell *tmp,*f;
    P(swap->s_sem);

    swap->start_text[new]=swap->text[old];
    swap->start_data[new]=swap->data[old];
    swap->start_stack[new]=swap->stack[old];

    for(tmp=swap->text[old];tmp!=NULL;tmp=tmp->next){
        if(tmp!=NULL){
            KASSERT(!tmp->load);
            KASSERT(!tmp->swap);
            tmp->swap=1;
            f=swap->free;
            swap->free=swap->free->next;
            f->next=swap->text[new];
            swap->text[new]=f;
        }
    }
    for(tmp=swap->data[old];tmp!=NULL;tmp=tmp->next){
        if(tmp!=NULL){
            KASSERT(!tmp->load);
            KASSERT(!tmp->swap);
            tmp->swap=1;
            f=swap->free;
            swap->free=swap->free->next;
            f->next=swap->data[new];
            swap->data[new]=f;
        }
    }
    for(tmp=swap->stack[old];tmp!=NULL;tmp=tmp->next){
        if(tmp!=NULL){
            KASSERT(!tmp->load);
            KASSERT(!tmp->swap);
            tmp->swap=1;
            f=swap->free;
            swap->free=swap->free->next;
            f->next=swap->stack[new];
            swap->stack[new]=f;
        }
    }

    V(swap->s_sem);
    
}

void end_copy_swap(pid_t newp){
    struct swap_cell *tmp;

    P(swap->s_sem);

    for(tmp=swap->start_text[newp];tmp!=NULL;tmp=tmp->next){
        if(tmp!=NULL){
            KASSERT(tmp->swap==1);
            tmp->swap=0;
        }
    }
    for(tmp=swap->start_data[newp];tmp!=NULL;tmp=tmp->next){
        if(tmp!=NULL){
            KASSERT(tmp->swap==1);
            tmp->swap=0;
        }
    }
    for(tmp=swap->start_stack[newp];tmp!=NULL;tmp=tmp->next){
        if(tmp!=NULL){
            KASSERT(tmp->swap==1);
            tmp->swap=0;
        }
    }

    V(swap->s_sem);
}