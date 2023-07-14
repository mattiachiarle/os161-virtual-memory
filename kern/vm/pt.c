#define VALBITZERO(a) (a & ~1)
#define VALBITONE(a) (a | 1)
#define GETVALBIT(a) (a & 1)
#define REFBITONE(a) (a | 2)
#define REFBITZERO(a) (a & ~2)
#define GETREFBIT(a) (a & 2)
#define TLBBITONE(a) (a | 4)
#define TLBBITZERO(a) (a & ~4)
#define GETTLBBIT(a) (a & 4)
#define IOBITONE(a) (a | 8)
#define IOBITZERO(a) (a & ~8)
#define GETIOBIT(a) (a & 8)
#define SWAPBITONE(a) (a | 16)
#define SWAPBITZERO(a) (a & ~16)
#define GETSWAPBIT(a) (a & 16)

#define KMALLOC_PAGE 1

#include "coremap.h"
#include "vm.h"
#include "mainbus.h"
#include "lib.h"
#include "cpu.h"
#include "types.h"
#include "spinlock.h"
#include "pt.h"
#include "segments.h"
#include "proc.h"
#include "current.h"
#include "vmstats.h"

int lastIndex = 0; // for FIFO, need to check priorities: Ref. bit and TLB bit
// for now idea: to get first addr use ram_stealmem or put visible firstaddr

void pt_init(void)
{
    spinlock_acquire(&stealmem_lock);
    int numFrames;
    nkmalloc=0;
    
    //This numframes will be higher than the actual number of frames available since it doesn't take into account the kmalloc.
    //However this won't cause errors since ptsize will be correctly initialized.
    numFrames = (mainbus_ramsize() - ram_stealmem(0)) / PAGE_SIZE; // get how many frames I have in RAM, remember: 1 IPT entry for each frame

    // the next one: how many entries in a frame, that is a ceil
    // the next one: number of frames for the page table = num of frames / how many entries in a frame, with ceil
    // allocate IPT in kern
    spinlock_release(&stealmem_lock);
    peps.pt = kmalloc(sizeof(struct pt_entry) * numFrames);
    spinlock_acquire(&stealmem_lock);
    if (peps.pt == NULL)
    {
        panic("error allocating IPT!!");
    }
    
    peps.pt_lock = lock_create("pagetable-lock");
    if(peps.pt_lock==NULL){
        panic("error!! lock not initialized...");
    }
    peps.pt_cv = cv_create("pagetable-cv");
    if(peps.pt_lock==NULL){
        panic("error!! cv not initialized...");
    }
    spinlock_release(&stealmem_lock);
    peps.contiguous = kmalloc(sizeof(int) * numFrames);
    spinlock_acquire(&stealmem_lock);
    if (peps.contiguous == NULL)
    {
        panic("error allocating contiguous!!");
    }
    for (int i = 0; i < numFrames; i++) // all validity bit to 0
    {
        peps.pt[i].ctl = 0;
        peps.contiguous[i]=-1;
        spinlock_release(&stealmem_lock);
        peps.pt[i].entry_lock = lock_create("entry_lock");
        peps.pt[i].entry_cv = cv_create("entry_cv");
        spinlock_acquire(&stealmem_lock);
    }

    peps.firstfreepaddr = ram_stealmem(0);
    DEBUG(DB_VM,"\nRam size :0x%x, first free address: 0x%x, available memory: 0x%x",mainbus_ramsize(),ram_stealmem(0),mainbus_ramsize()-ram_stealmem(0));
    // modify in order to take only locatable free space
    peps.ptSize = ((mainbus_ramsize() - ram_stealmem(0)) / PAGE_SIZE) - 1;
    pt_active=1;
    // spinlock_init(&peps.test);
    // peps.sem = sem_create("sem_pt",1);

    spinlock_release(&stealmem_lock);
}

static int findspace()
{
    int val = -1;
    for (int i = 0; i < peps.ptSize; i++)
    {
        val = GETVALBIT(peps.pt[i].ctl); // 1 if validity bit=1
        if (!val && peps.pt[i].page!=KMALLOC_PAGE && !GETIOBIT(peps.pt[i].ctl) && !GETSWAPBIT(peps.pt[i].ctl))
        {
            return i; // return the position of empty entry in PT
        }
    }
    return -1;
}

#if OPT_DEBUG
static int n=0;
#endif

int find_victim(vaddr_t vaddr, pid_t pid, int spl)
{
    int i, start_i=lastIndex, first=0, old_validity=0;
    pid_t old_pid;
    vaddr_t old_v;
    #if OPT_DEBUG
    if(n==0){
        DEBUG(DB_VM,"FIRST FIND VICTIM\n");
        n++;
    }
    #endif
    // if I am here there will be a replacement since all pages are valid
    for (i = lastIndex;; i = (i + 1) % peps.ptSize)
    {
        // Val, Ref,TLB bits from low to high
        if (peps.pt[i].page!=KMALLOC_PAGE && !GETTLBBIT(peps.pt[i].ctl) && !GETIOBIT(peps.pt[i].ctl) && !GETSWAPBIT(peps.pt[i].ctl)) // if isInTLB = 0
        {
            if (GETREFBIT(peps.pt[i].ctl) == 0) // if Ref bit==0 victim found
            {
                KASSERT(!GETTLBBIT(peps.pt[i].ctl));
                KASSERT(!GETIOBIT(peps.pt[i].ctl));
                KASSERT(!GETSWAPBIT(peps.pt[i].ctl));
                KASSERT(peps.pt[i].page!=KMALLOC_PAGE);
                // KASSERT(GETVALBIT(peps.pt[i].ctl));
                old_pid=peps.pt[i].pid;
                peps.pt[i].pid=pid;
                old_validity=GETVALBIT(peps.pt[i].ctl);
                peps.pt[i].ctl = IOBITONE(peps.pt[i].ctl);
                peps.pt[i].ctl = VALBITONE(peps.pt[i].ctl);
                old_v = peps.pt[i].page;
                peps.pt[i].page = vaddr;
                if(old_validity){
                    store_swap(old_v,old_pid,i * PAGE_SIZE + peps.firstfreepaddr);
                }
                lastIndex = (i + 1) % peps.ptSize;// new ptr for FIFO
                return i; // return paddr of that frame
            }
            else
            {                                                // found rb==1, so-->
                peps.pt[i].ctl = REFBITZERO(peps.pt[i].ctl); // set RB to 0 and continue
            }
        }
        if((i + 1) % peps.ptSize == start_i){
            if(first==0){
                first=1;
                continue;
            }
            else{
                lock_acquire(peps.pt_lock);
                splx(spl);
                cv_wait(peps.pt_cv,peps.pt_lock);
                spl=splhigh();
                lock_release(peps.pt_lock);
            }
        }
    }
    panic("no victims! it's a problem...");
}

// some bits for control; from the lower:  Validity bit, Reference bit, isInTLB bit, ...
//  bitStatus = (j >> n) & 1;

paddr_t get_page(vaddr_t v, int spl)
{

    pid_t pid = proc_getpid(curproc); // get curpid here
    int res;
    paddr_t pp;
    res = pt_get_paddr(v, pid, spl);

    if (res != -1)
    {
        pp = (paddr_t) res;
        add_tlb_reload();
        return pp;
    }

    DEBUG(DB_VM,"PID=%d wants to load 0x%x\n",pid,v);

    int pos = findspace(v,pid); // find a free space
    if (pos == -1)
    {
        pos = find_victim(v, pid, spl);
        KASSERT(pos<peps.ptSize);
        pp = peps.firstfreepaddr + pos*PAGE_SIZE;
    }
    else{
        KASSERT(pos<peps.ptSize);
        pp = peps.firstfreepaddr + pos*PAGE_SIZE;
        peps.pt[pos].ctl = VALBITONE(peps.pt[pos].ctl);
        peps.pt[pos].ctl = IOBITONE(peps.pt[pos].ctl);
        peps.pt[pos].page = v;
        peps.pt[pos].pid = pid;
    }

    KASSERT(peps.pt[pos].page!=KMALLOC_PAGE);
    load_page(v, pid, pp, spl);
    peps.pt[pos].ctl = IOBITZERO(peps.pt[pos].ctl);
    lock_acquire(peps.pt[pos].entry_lock);
    cv_broadcast(peps.pt[pos].entry_cv,peps.pt[pos].entry_lock);
    lock_release(peps.pt[pos].entry_lock);
    lock_acquire(peps.pt_lock);
    cv_broadcast(peps.pt_cv,peps.pt_lock);
    lock_release(peps.pt_lock);
    peps.pt[pos].ctl = TLBBITONE(peps.pt[pos].ctl);

    return pp;
}

int pt_get_paddr(vaddr_t v, pid_t p, int spl)
{
    int validity, stopped;

    stopped=1;

    while(stopped){
        stopped=0;
        for (int i = 0; i < peps.ptSize; i++)
        {
            validity = GETVALBIT(peps.pt[i].ctl);
                if (v == peps.pt[i].page && peps.pt[i].pid == p && validity)
                {
                    lock_acquire(peps.pt[i].entry_lock);
                    while(GETIOBIT(peps.pt[i].ctl)){
                        stopped=1;
                        splx(spl);
                        cv_wait(peps.pt[i].entry_cv,peps.pt[i].entry_lock);
                        spl = splhigh();
                    }
                    lock_release(peps.pt[i].entry_lock);
                    if (v != peps.pt[i].page || peps.pt[i].pid != p || !GETVALBIT(peps.pt[i].ctl)){
                        continue;
                    }
                    KASSERT(!GETIOBIT(peps.pt[i].ctl));
                    KASSERT(!GETTLBBIT(peps.pt[i].ctl));
                    KASSERT(peps.pt[i].page!=KMALLOC_PAGE);
                    peps.pt[i].ctl = TLBBITONE(peps.pt[i].ctl); // set isInTLB to 1, third ctl bit
                    return i * PAGE_SIZE + peps.firstfreepaddr; // send the paddr found
                }
        }
    }
    // else insert in IPT the right varrd+pid from Mattia el Chiurlo

    return -1; // not found in PT
}

void free_pages(pid_t p)
{

    for (int i = 0; i < peps.ptSize; i++)
    {
        if (peps.pt[i].pid == p && GETVALBIT(peps.pt[i].ctl) && peps.pt[i].page!=KMALLOC_PAGE) // if it is valid and it is the same pid passed
        {
            KASSERT(peps.pt[i].page!=KMALLOC_PAGE);
            KASSERT(!GETSWAPBIT(peps.pt[i].ctl));
            KASSERT(!GETIOBIT(peps.pt[i].ctl));
            peps.pt[i].ctl = 0;
            peps.pt[i].page = 0;
            peps.pt[i].pid = 0;
        }
    }

}

int cabodi(vaddr_t v, pid_t pid)
{

    int i;

    DEBUG(DB_VM,"Cabodi was called with vaddr=0x%x, pid=%d\n",v,pid);

    for (i = 0; i < peps.ptSize; i++)
    {
        if (peps.pt[i].page == v && peps.pt[i].pid==pid && GETVALBIT(peps.pt[i].ctl))
        {
            if(!(peps.pt[i].ctl & 4)){
                kprintf("Error for process %d, vaddr 0x%x, ctl=0x%x\n",pid,v,peps.pt[i].ctl);
            }
            KASSERT(peps.pt[i].page!=KMALLOC_PAGE);
            KASSERT((peps.pt[i].ctl & 4));               // it must be inside TLB
            peps.pt[i].ctl = TLBBITZERO(peps.pt[i].ctl); // remove TLB bit
            peps.pt[i].ctl = REFBITONE(peps.pt[i].ctl);  // set RB to 1

            return 1;                                    
        }
    }

    return -1;
}

static int valid_entry(uint8_t ctl, vaddr_t v){
    if(GETTLBBIT(ctl)){
        return 1;
    }
    if(GETVALBIT(ctl) && GETREFBIT(ctl)){
        return 1;
    }
    if(v==KMALLOC_PAGE){
        return 1;
    }
    if(GETIOBIT(ctl)){
        return 1;
    }
    if(GETSWAPBIT(ctl)){
        return 1;
    }
    return 0;
}

#if OPT_DEBUG
static int nfork=0;
#endif

paddr_t get_contiguous_pages(int npages, int spl){

    DEBUG(DB_VM,"Process %d performs kmalloc for %d pages\n", curproc->p_pid,npages);

    if(npages==1){
        paddr_t pp;
        int pos = findspace(KMALLOC_PAGE,curproc->p_pid); // find a free space
        if (pos == -1)
        {
            pos = find_victim(KMALLOC_PAGE, curproc->p_pid, spl);
            peps.pt[pos].ctl = IOBITZERO(peps.pt[pos].ctl);
            lock_acquire(peps.pt[pos].entry_lock);
            cv_broadcast(peps.pt[pos].entry_cv,peps.pt[pos].entry_lock);
            lock_release(peps.pt[pos].entry_lock);
            lock_acquire(peps.pt_lock);
            cv_broadcast(peps.pt_cv,peps.pt_lock);
            lock_release(peps.pt_lock);
            KASSERT(pos<peps.ptSize);
            pp = peps.firstfreepaddr + pos*PAGE_SIZE;
        }
        else{
            KASSERT(pos<peps.ptSize);
            pp = peps.firstfreepaddr + pos*PAGE_SIZE;
            peps.pt[pos].ctl = VALBITONE(peps.pt[pos].ctl);
            peps.pt[pos].page = KMALLOC_PAGE;
            peps.pt[pos].pid = curproc->p_pid;
        }

        peps.contiguous[pos]=1;

        return pp;
    }

    int i, j, first=-1, valid, prev=0, old_val, first_iteration=0;
    vaddr_t old_v;
    pid_t old_pid;

    if(npages>peps.ptSize){
        panic("Not enough memory for kmalloc");
    }

    for(i=0;i<peps.ptSize;i++){
        valid = GETVALBIT(peps.pt[i].ctl);
        if(i!=0){
            prev = valid_entry(peps.pt[i-1].ctl,peps.pt[i-1].page);
        }
        if(!valid && GETTLBBIT(peps.pt[i].ctl)==0 && peps.pt[i].page!=KMALLOC_PAGE && !GETIOBIT(peps.pt[i].ctl) && !GETSWAPBIT(peps.pt[i].ctl) && (i==0 || prev)){
            first=i;
        }
        int io = GETIOBIT(peps.pt[i].ctl);
        if(first>=0 && !valid && GETTLBBIT(peps.pt[i].ctl)==0 && !GETSWAPBIT(peps.pt[i].ctl) && peps.pt[i].page!=KMALLOC_PAGE && io==0 && i-first==npages-1){
            DEBUG(DB_VM,"Kmalloc for process %d entry%d\n",curproc->p_pid,first);
            for(j=first;j<=i;j++){
                KASSERT(peps.pt[j].page!=KMALLOC_PAGE);
                KASSERT(!GETTLBBIT(peps.pt[j].ctl));
                KASSERT(!GETVALBIT(peps.pt[j].ctl));
                KASSERT(!GETIOBIT(peps.pt[j].ctl));
                KASSERT(!GETSWAPBIT(peps.pt[i].ctl));
                peps.pt[j].ctl = VALBITONE(peps.pt[j].ctl); //Set pages as valid
                peps.pt[j].page = KMALLOC_PAGE;
                peps.pt[j].pid = curproc->p_pid;
                //vaddr and pid are useless here since kernel uses a different address translation
            }
            peps.contiguous[first]=npages;
            return first*PAGE_SIZE + peps.firstfreepaddr;
        }
    }

    //If we arrive here it means that the page table is full, so we must perform victim selection.

    #if OPT_DEBUG
    if(nfork==0){
        kprintf("FIRST FORK WITH REPLACE\n");
        nfork++;
    }
    #endif

    while(1){
        for (i = lastIndex; i < peps.ptSize; i ++)
        {
            // Val, Ref,TLB bits from low to high
            if (peps.pt[i].page!=KMALLOC_PAGE && GETTLBBIT(peps.pt[i].ctl) == 0 && !GETIOBIT(peps.pt[i].ctl) && !GETSWAPBIT(peps.pt[i].ctl)) // if isInTLB = 0
            {
                if(GETREFBIT(peps.pt[i].ctl) && GETVALBIT(peps.pt[i].ctl)){
                    peps.pt[i].ctl = REFBITZERO(peps.pt[i].ctl);
                    continue;
                }
                if ((GETREFBIT(peps.pt[i].ctl) == 0 || GETVALBIT(peps.pt[i].ctl) == 0) && (i==0 || valid_entry(peps.pt[i-1].ctl,peps.pt[i-1].page)))
                {
                    first = i;
                }
                if(first>=0 && (GETREFBIT(peps.pt[i].ctl) == 0 || GETVALBIT(peps.pt[i].ctl) == 0) && i-first==npages-1){
                    DEBUG(DB_VM,"Found a space for a kmalloc for process %d entry%d\n",curproc->p_pid,first);
                    for(j=first;j<=i;j++){
                        KASSERT(peps.pt[j].page!=KMALLOC_PAGE);
                        KASSERT(!GETTLBBIT(peps.pt[j].ctl));
                        KASSERT(!GETREFBIT(peps.pt[j].ctl) || !GETVALBIT(peps.pt[j].ctl));
                        KASSERT(!GETIOBIT(peps.pt[j].ctl));
                        KASSERT(!GETSWAPBIT(peps.pt[j].ctl));
                        old_pid = peps.pt[j].pid;
                        old_v = peps.pt[j].page;
                        peps.pt[j].pid = curproc->p_pid;
                        peps.pt[j].page = KMALLOC_PAGE;
                        old_val=GETVALBIT(peps.pt[j].ctl);
                        peps.pt[j].ctl = VALBITONE(peps.pt[j].ctl); //Set pages as valid
                        if(old_val){
                            peps.pt[j].ctl = IOBITONE(peps.pt[j].ctl);
                            store_swap(old_v,old_pid,j * PAGE_SIZE + peps.firstfreepaddr);
                            peps.pt[j].ctl = IOBITZERO(peps.pt[j].ctl);
                            lock_acquire(peps.pt[j].entry_lock);
                            cv_broadcast(peps.pt[j].entry_cv,peps.pt[j].entry_lock);
                            lock_release(peps.pt[j].entry_lock);
                            lock_acquire(peps.pt_lock);
                            cv_broadcast(peps.pt_cv,peps.pt_lock);
                            lock_release(peps.pt_lock);
                        }
                    }
                    peps.contiguous[first]=npages;
                    lastIndex = (i + 1) % peps.ptSize;
                    return first*PAGE_SIZE + peps.firstfreepaddr;
                }
            }
        }
        lastIndex=0;

        if(first_iteration<2){
            first_iteration++;
        }
        else{
            lock_acquire(peps.pt_lock);
            splx(spl);
            cv_wait(peps.pt_cv,peps.pt_lock);
            spl=splhigh();
            lock_release(peps.pt_lock);
        }

        first=-1;
    }

    return ENOMEM;
}

void free_contiguous_pages(vaddr_t addr){
    
    int i, index, niter;

    paddr_t p = KVADDR_TO_PADDR(addr);

    index = (p-peps.firstfreepaddr)/PAGE_SIZE;
    niter = peps.contiguous[index];

    DEBUG(DB_VM,"Process %d performs kfree for %d pages\n", curproc?curproc->p_pid:0,niter);

    nkmalloc-=niter;
    KASSERT(niter!=-1);

    for(i=index;i<index+niter;i++){
        KASSERT(peps.pt[i].page==KMALLOC_PAGE);
        peps.pt[i].ctl = VALBITZERO(peps.pt[i].ctl);
        peps.pt[i].page=0;
    }

    peps.contiguous[index]=-1;

    DEBUG(DB_VM,"New kmalloc number after free=%d\n",nkmalloc);
}

void copy_pt_entries(pid_t old, pid_t new){

    // int pos;

    for(int i=0;i<peps.ptSize;i++){
        if(peps.pt[i].pid==old && GETVALBIT(peps.pt[i].ctl) && peps.pt[i].page!=KMALLOC_PAGE){
            // pos = findspace();
            // if(pos==-1){
                // spinlock_release(&peps.test);
                KASSERT(!GETIOBIT(peps.pt[i].ctl));
                KASSERT(GETSWAPBIT(peps.pt[i].ctl));
                KASSERT(peps.pt[i].page!=KMALLOC_PAGE);
                peps.pt[i].ctl = IOBITONE(peps.pt[i].ctl);
                // kprintf("Copied from pt address 0x%x for process %d\n",peps.pt[i].page,new);
                store_swap(peps.pt[i].page,new,peps.firstfreepaddr+i*PAGE_SIZE);
                peps.pt[i].ctl = IOBITZERO(peps.pt[i].ctl);
                // lock_acquire(peps.pt[i].entry_lock);
                // cv_broadcast(peps.pt[i].entry_cv,peps.pt[i].entry_lock);
                // lock_release(peps.pt[i].entry_lock);
                // lock_acquire(peps.pt_lock);
                // cv_broadcast(peps.pt_cv,peps.pt_lock);
                // lock_release(peps.pt_lock);
                // peps.pt[i].ctl = SWAPBITZERO(peps.pt[i].ctl);
                // spinlock_acquire(&peps.test);

            // }
            // else{
            //     peps.pt[pos].ctl = VALBITONE(peps.pt[pos].ctl);
            //     //peps.pt[pos].ctl = TLBBITONE(peps.pt[pos].ctl);
            //     peps.pt[pos].page = peps.pt[i].page;
            //     peps.pt[pos].pid = new;
            //     memmove((void *)PADDR_TO_KVADDR(peps.firstfreepaddr + pos*PAGE_SIZE),(void *)PADDR_TO_KVADDR(peps.firstfreepaddr + i*PAGE_SIZE), PAGE_SIZE);
            //     KASSERT(!GETIOBIT(peps.pt[pos].ctl));
            //     KASSERT(!GETTLBBIT(peps.pt[pos].ctl));
            //     KASSERT(!GETSWAPBIT(peps.pt[pos].ctl));
            //     KASSERT(peps.pt[pos].page!=KMALLOC_PAGE);
            // }
        }
    }

    #if OPT_DEBUG
    print_list(new);
    #endif

}

void prepare_copy_pt(pid_t pid){

    for(int i=0;i<peps.ptSize;i++){
        if(peps.pt[i].pid == pid && peps.pt[i].page!=KMALLOC_PAGE && GETVALBIT(peps.pt[i].ctl)){
            KASSERT(!GETIOBIT(peps.pt[i].ctl));
            peps.pt[i].ctl = SWAPBITONE(peps.pt[i].ctl);
        }
    }
}

void end_copy_pt(pid_t pid){

    for(int i=0;i<peps.ptSize;i++){
        if(peps.pt[i].pid == pid && peps.pt[i].page!=KMALLOC_PAGE && GETVALBIT(peps.pt[i].ctl)){
            KASSERT(GETSWAPBIT(peps.pt[i].ctl));
            peps.pt[i].ctl = SWAPBITZERO(peps.pt[i].ctl);
            lock_acquire(peps.pt[i].entry_lock);
            cv_broadcast(peps.pt[i].entry_cv,peps.pt[i].entry_lock);
            lock_release(peps.pt[i].entry_lock);
        }
    }

    lock_acquire(peps.pt_lock);
    cv_broadcast(peps.pt_cv,peps.pt_lock);
    lock_release(peps.pt_lock);

}

// void free_forgotten_pages(void){

//     for(int i=0;i<peps.ptSize;i++){
//         if(peps.pt[i].page==KMALLOC_PAGE && peps.pt[i].pid!=1){
//             peps.pt[i].ctl = VALBITZERO(peps.pt[i].ctl);
//             peps.pt[i].page=0;
//             peps.contiguous[i]=-1;
//         }
//     }

// }

void print_nkmalloc(void){
    kprintf("Final number of kmalloc: %d\n",nkmalloc);
}