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

#define KMALLOC_PAGE 1 //Since all the valid pages will end with 0x...000, we are sure that no entry will have 0x1 as value

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

int lastIndex = 0; //Used to implement second chance replacement policy

void pt_init(void)
{
    spinlock_acquire(&stealmem_lock);
    int numFrames;
    #if OPT_DEBUG
    nkmalloc=0;
    #endif
    
    //This numframes will be higher than the actual number of frames available since it doesn't take into account the kmalloc.
    //However this won't cause errors since ptsize will be correctly initialized.
    numFrames = (mainbus_ramsize() - ram_stealmem(0)) / PAGE_SIZE; // get how many frames I have in RAM, remember: 1 IPT entry for each frame

    spinlock_release(&stealmem_lock); //We need to release the spinlock before kmalloc to avoid deadlock
    peps.pt = kmalloc(sizeof(struct pt_entry) * numFrames);//One entry for each available frame
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
    for (int i = 0; i < numFrames; i++) // We initialize all the entries with default values
    {
        peps.pt[i].ctl = 0;
        peps.contiguous[i]=-1;
    }

    DEBUG(DB_VM,"Ram size :0x%x, first free address: 0x%x, available memory: 0x%x",mainbus_ramsize(),ram_stealmem(0),mainbus_ramsize()-ram_stealmem(0));

    peps.ptSize = ((mainbus_ramsize() - ram_stealmem(0)) / PAGE_SIZE) - 1; //ram_stealmem(0) allows us to get the first free physical address, i.e. from where our IPT starts.    
    peps.firstfreepaddr = ram_stealmem(0);

    pt_active=1; //We configured correctly our IPT, so from now on kmalloc operations can be handled by it.

    spinlock_release(&stealmem_lock);
}

void htable_init(void){
    htable.size = 2 * peps.ptSize;  // size of hash table   

    htable.table = kmalloc(sizeof(struct hashentry *) * htable.size); // alloc hash table
    for (int ii = 0; ii < htable.size; ii++) 
    {
        htable.table[ii] = NULL; // no pointers for now inside the array of lists
    }

    unusedptrlist = NULL;  
    struct hashentry *tmp; 
    for (int jj = 0; jj <  peps.ptSize; jj++) // initialize unused ptr list
    {
        tmp = kmalloc(sizeof(struct hashentry));
        KASSERT((unsigned int)tmp>0x80000000);
        if (!tmp)
        {
            panic("Error during hashpt elements allocation");
        }
        tmp->next = unusedptrlist; 
        unusedptrlist = tmp;
    }  // fill all the unused ptr list with size ptSize

}

static int findspace()
{
    int val = -1;
    for (int i = 0; i < peps.ptSize; i++)
    {
        val = GETVALBIT(peps.pt[i].ctl); // 1 if validity bit=1
        if (!val && peps.pt[i].page!=KMALLOC_PAGE && !GETIOBIT(peps.pt[i].ctl) && !GETSWAPBIT(peps.pt[i].ctl)) //These are all the conditions that make a page not free, i.e. not removable
        {
            return i; // return the position of empty entry in PT
        }
    }
    return -1;
}

#if OPT_DEBUG
static int n=0;
#endif

int find_victim(vaddr_t vaddr, pid_t pid)
{
    int i, start_i=lastIndex, niter=0, old_validity=0;
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
        if (peps.pt[i].page!=KMALLOC_PAGE && !GETTLBBIT(peps.pt[i].ctl) && !GETIOBIT(peps.pt[i].ctl) && !GETSWAPBIT(peps.pt[i].ctl)) //If so the page can be swapped out
        {
            if (GETREFBIT(peps.pt[i].ctl) == 0) // if Ref bit==0 victim found
            {
                KASSERT(!GETTLBBIT(peps.pt[i].ctl));
                KASSERT(!GETIOBIT(peps.pt[i].ctl));
                KASSERT(!GETSWAPBIT(peps.pt[i].ctl));
                KASSERT(peps.pt[i].page!=KMALLOC_PAGE);
                old_pid=peps.pt[i].pid; //Due to issues with synchronization, we need to set all the new values before load/store operations, i.e. before sleeping. However, we save the old values before modifying them to use them in the future store.
                peps.pt[i].pid=pid;
                old_validity=GETVALBIT(peps.pt[i].ctl);
                peps.pt[i].ctl = IOBITONE(peps.pt[i].ctl); //We'll perform an I/O operation (for sure read, and if necessary store too)
                peps.pt[i].ctl = VALBITONE(peps.pt[i].ctl);
                old_v = peps.pt[i].page;
                peps.pt[i].page = vaddr;
                if(old_validity){ //If the page was valid we save it in the swapfile before proceeding
                    remove_from_hash(old_v, old_pid); //We remove the page from the hash table too
                    store_swap(old_v,old_pid,i * PAGE_SIZE + peps.firstfreepaddr);
                } 
                add_in_hash(vaddr, pid, i); //We add the new page to the hash table
                lastIndex = (i + 1) % peps.ptSize; //New index for second chance
                return i; // return index of that frame
            }
            else
            {                                                // found rb==1, so-->
                peps.pt[i].ctl = REFBITZERO(peps.pt[i].ctl); // set RB to 0 and continue
            }
        }
        if((i + 1) % peps.ptSize == start_i){
            if(niter<2){ //We allow 2 full iterations on the IPT without interruptions to find a victim
                niter++;
                continue;
            }
            else{ //We didn't find any victim. Since sizeof(IPT)>sizeof(TLB)+n_kmallocs, it means that there are potential victims but they can't be removed because currently they're involved in a I/O or in a swap.
                  //To avoid starvation (that would cause a deadlock since we disabled the interrupts) we wait until something changes.
                lock_acquire(peps.pt_lock);
                cv_wait(peps.pt_cv,peps.pt_lock); //To wait, we wait on the condition variable.
                lock_release(peps.pt_lock);
                niter=0; //We allow again 2 iterations
            }
        }
    }
    panic("no victims! it's a problem...");
}

int get_hash_func(vaddr_t v, pid_t p)
{
    int val = (((int)v) % 24) + ((((int)p) % 8) << 24);
    val = val ^1234567891;
    val = val % htable.size;
    return val;
}

#if OPT_DEBUG
static int add=0;
#endif

void add_in_hash(vaddr_t vad, pid_t pid, int pos) // NEW - pos = position of the IPT
{
    KASSERT(vad!=0);
    KASSERT(pid!=0);
    #if OPT_DEBUG
    add++;
    #endif
    DEBUG(DB_VM,"Adding in hash 0x%x for process %d, pos %d\n",vad,pid,pos);
    int val = get_hash_func(vad, pid); //We get the index to use to access the hash table
    struct hashentry *tmp = unusedptrlist; // take the first element from the free list
    KASSERT(tmp!=NULL);
    unusedptrlist = tmp->next; //remove from head
    tmp->vad = vad; // update values
    tmp->pid = pid; 
    tmp->iptentry = pos;
    tmp->next = htable.table[val]; // insert in hashtable 
    htable.table[val] = tmp;       // attach in head here
    DEBUG(DB_VM,"Allocated tmp in val = %d, 0x%x. Next: 0x%x\n",val, (unsigned int)tmp, (unsigned int)tmp->next);
}

int get_index_from_hash(vaddr_t vad, pid_t pid)
{
    int val = get_hash_func(vad, pid);
    struct hashentry *tmp = htable.table[val]; //from the array hashtable, take the correct list
    #if OPT_DEBUG
    if(tmp!=NULL)
        kprintf("Value of tmp: 0x%x, pid=%d\n",tmp->vad,tmp->pid);
    #endif
    while (tmp != NULL)   
    {
        KASSERT((unsigned int)tmp>0x80000000 && (unsigned int)tmp<=0x90000000);
        if (tmp->vad == vad && tmp->pid == pid) 
        {
            return tmp->iptentry; // return the correct value
        }
        tmp = tmp->next;
    }
    return -1; //We didn't find any entry, so the accessed vad is not in the IPT currently
}

#if OPT_DEBUG
static int rem=0;
#endif

void remove_from_hash(vaddr_t vad, pid_t pid) // insert again in unusedptrlist and REMOVE from hash table
{    
    #if OPT_DEBUG   
    rem++;
    #endif
    int val = get_hash_func(vad, pid); 
    DEBUG(DB_VM,"Removing from hash 0x%x for process %d, pos %d\n",vad,pid,val);

    struct hashentry *tmp = htable.table[val]; //We get the head of the list
    struct hashentry *prev=NULL; //Previous entry 
    if (tmp == NULL)
    {
        panic("Error during remove from hash"); //Error: we tried to remove an entry that was never inserted
    }

    if (tmp->vad == vad && tmp->pid == pid) // if found in head remove instantly
    {
        tmp->vad=0;
        tmp->pid=0;
        htable.table[val] = tmp->next; /// take second in list
        tmp->next = unusedptrlist;
        unusedptrlist = tmp;
        return;
    }

    prev=tmp; //We already checked the first element, so we can update prev and tmp
    tmp = tmp->next; 

    while (tmp != NULL)
    {
        if (tmp->vad == vad && tmp->pid == pid) // if found
        {
            tmp->vad=0; //Reset the values
            tmp->pid=0;
            tmp->iptentry = -1;
            prev->next = tmp->next; // remove from the list
            tmp->next = unusedptrlist;
            unusedptrlist = tmp; // update in head list of unused ptrs
            return;
        }
        prev = tmp; //We update prev and tmp
        tmp = tmp->next;
    }

    panic("nothing to remove found!!");
}

paddr_t get_page(vaddr_t v)
{

    pid_t pid = proc_getpid(curproc); // get curpid here
    int res;
    paddr_t pp;
    res = pt_get_paddr(v, pid); //We search if the page is already in the page table

    if (res != -1)
    {
        pp = (paddr_t) res;
        add_tlb_reload();
        return pp;
    }

    DEBUG(DB_VM,"PID=%d wants to load 0x%x\n",pid,v);

    int pos = findspace(v,pid); // find a free space
    if (pos == -1) //No free space, so we select the victim
    {
        pos = find_victim(v, pid);
        KASSERT(pos<peps.ptSize);
        pp = peps.firstfreepaddr + pos*PAGE_SIZE; //We compute the physical address
    }
    else{
        KASSERT(pos<peps.ptSize);
        add_in_hash(v, pid, pos); //We add an entry in the hash table
        pp = peps.firstfreepaddr + pos*PAGE_SIZE;
        peps.pt[pos].ctl = VALBITONE(peps.pt[pos].ctl); //Now the page is valid
        peps.pt[pos].ctl = IOBITONE(peps.pt[pos].ctl); //We'll perform an I/O to load the page, so we set IOBIT
        peps.pt[pos].page = v;
        peps.pt[pos].pid = pid;
    }

    KASSERT(peps.pt[pos].page!=KMALLOC_PAGE);
    load_page(v, pid, pp); //We load the page from the swapfile or from the ELF file
    peps.pt[pos].ctl = IOBITZERO(peps.pt[pos].ctl); //We ended the I/O
    peps.pt[pos].ctl = TLBBITONE(peps.pt[pos].ctl); //The entry will be added in the TLB, so we set the TLB bit

    return pp;
}

int pt_get_paddr(vaddr_t v, pid_t p)
{
    int i = get_index_from_hash(v, p);//We search for the entry in the page table
    if(i==-1){
        return i; //Entry not found, so we return -1
    }
    KASSERT(peps.pt[i].page==v);
    KASSERT(peps.pt[i].pid==p);
    KASSERT(!GETIOBIT(peps.pt[i].ctl));
    KASSERT(!GETTLBBIT(peps.pt[i].ctl));
    KASSERT(peps.pt[i].page!=KMALLOC_PAGE);
    peps.pt[i].ctl = TLBBITONE(peps.pt[i].ctl); // set isInTLB to 1
    return i * PAGE_SIZE + peps.firstfreepaddr; // send the paddr found

}

void free_pages(pid_t p)
{

    for (int i = 0; i < peps.ptSize; i++)
    {
        if (peps.pt[i].pid == p && GETVALBIT(peps.pt[i].ctl) && peps.pt[i].page!=KMALLOC_PAGE) //We don't free kmalloc pages when a process ends to avoid errors with kmalloc function
        {
            KASSERT(peps.pt[i].page!=KMALLOC_PAGE);
            KASSERT(!GETSWAPBIT(peps.pt[i].ctl));
            KASSERT(!GETIOBIT(peps.pt[i].ctl));
            remove_from_hash(peps.pt[i].page, peps.pt[i].pid); //We remove the entry from the page table
            peps.pt[i].ctl = 0;
            peps.pt[i].page = 0;
            peps.pt[i].pid = 0;
        }
    }

    lock_acquire(peps.pt_lock);
    cv_broadcast(peps.pt_cv,peps.pt_lock); //We freed some entries in the page table, so we wake up the processes waiting on the cv of the IPT.
    lock_release(peps.pt_lock);

    #if OPT_DEBUG
    DEBUG(DB_VM,"We have %d add and %d remove\n",add,rem);

    struct hashentry *tmp;

    for(int i=0;i < htable.size; i++){ //We check that all the pages of a process were correctly freed
        tmp=htable.table[i];
        while(tmp!=NULL){
            if(htable.table[i]->vad!=KMALLOC_PAGE){
                kprintf("Error with a frame in the hash table: index %d, vaddr %d, pid %d\n",i,htable.table[i]->vad,htable.table[i]->pid);
            }
            tmp=tmp->next;
        }
    }

    #endif

}

int update_tlb_bit(vaddr_t v, pid_t pid)
{

    int i;

    DEBUG(DB_VM,"Cabodi was called with vaddr=0x%x, pid=%d\n",v,pid);

    for (i = 0; i < peps.ptSize; i++)
    {
        if (peps.pt[i].page == v && peps.pt[i].pid==pid && GETVALBIT(peps.pt[i].ctl)) //We found the page that we were searching for
        {
            if(!(peps.pt[i].ctl & 4)){
                kprintf("Error for process %d, vaddr 0x%x, ctl=0x%x\n",pid,v,peps.pt[i].ctl);
            }
            KASSERT(peps.pt[i].page!=KMALLOC_PAGE);
            KASSERT(GETTLBBIT(peps.pt[i].ctl)); // it must be inside TLB
            peps.pt[i].ctl = TLBBITZERO(peps.pt[i].ctl); // remove TLB bit
            peps.pt[i].ctl = REFBITONE(peps.pt[i].ctl);  // set RB to 1

            return 1;                                    
        }
    }

    return -1;
}

/**
 * This function checks if a given entry is valid, i.e. if it can be removed or not.
 * 
 * @param ctl: ctl byte of the entry
 * @param v: virtual address of the entry
 * 
 * @return 1 if the page is valid (i.e. it can't be removed), 0 otherwise
*/
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

paddr_t get_contiguous_pages(int npages){
    
    DEBUG(DB_VM,"Process %d performs kmalloc for %d pages\n", curproc->p_pid,npages);

    int i, j, first=-1, valid, prev=0, old_val, first_iteration=0;
    vaddr_t old_v;
    pid_t old_pid;

    if (npages > peps.ptSize)
    {
        panic("Not enough memory for kmalloc"); //Impossible allocation
    }

    //FIRST STEP: search for npages contiguous non valid entries (to avoid swapping out) 

    for (i = 0; i < peps.ptSize; i++)
    {
        valid = GETVALBIT(peps.pt[i].ctl);
        if(i!=0){
            prev = valid_entry(peps.pt[i-1].ctl,peps.pt[i-1].page); //We check the validity of the previous entry
        }
        if(!valid && GETTLBBIT(peps.pt[i].ctl)==0 && peps.pt[i].page!=KMALLOC_PAGE && !GETIOBIT(peps.pt[i].ctl) && !GETSWAPBIT(peps.pt[i].ctl) && (i==0 || prev)){
            first=i; //If the current entry is not valid while the previous one was valid (or if the first entry is not valid) i becomes the beginning of the interval
        }
        if(first>=0 && !valid && GETTLBBIT(peps.pt[i].ctl)==0 && !GETSWAPBIT(peps.pt[i].ctl) && peps.pt[i].page!=KMALLOC_PAGE && !GETIOBIT(peps.pt[i].ctl) && i-first==npages-1){ //We found npages contiguous entries not valid
            DEBUG(DB_VM,"Kmalloc for process %d entry%d\n",curproc->p_pid,first);
            for(j=first;j<=i;j++){
                KASSERT(peps.pt[j].page!=KMALLOC_PAGE);
                KASSERT(!GETTLBBIT(peps.pt[j].ctl));
                KASSERT(!GETVALBIT(peps.pt[j].ctl));
                KASSERT(!GETIOBIT(peps.pt[j].ctl));
                KASSERT(!GETSWAPBIT(peps.pt[i].ctl));
                peps.pt[j].ctl = VALBITONE(peps.pt[j].ctl); //Set pages as valid
                peps.pt[j].page = KMALLOC_PAGE; //To remember that this page can't be swapped out until when we perform a free
                peps.pt[j].pid = curproc->p_pid;
                //vaddr and pid are useless here since kernel uses a different address translation (i.e. it doesn't access the IPT to get their physical address)
                //Please notice that we don't add in hash pages allocated with kmalloc since to access them we don't access the IPT, so it would be useless
            }
            peps.contiguous[first] = npages; //We save in position first the number of contiguous pages allocated. It'll be useful while freeing
            return first * PAGE_SIZE + peps.firstfreepaddr;
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
            if (peps.pt[i].page!=KMALLOC_PAGE && GETTLBBIT(peps.pt[i].ctl) == 0 && !GETIOBIT(peps.pt[i].ctl) && !GETSWAPBIT(peps.pt[i].ctl)) //We check if the entry can be considered for removal (all these conditions are related to pages that must be left in their position)
            {
                if(GETREFBIT(peps.pt[i].ctl) && GETVALBIT(peps.pt[i].ctl)){ //If the page is valid and has reference=1 we set reference=0 (due to second chance algorithm) and we continue
                    peps.pt[i].ctl = REFBITZERO(peps.pt[i].ctl);
                    continue;
                }
                if ((GETREFBIT(peps.pt[i].ctl) == 0 || GETVALBIT(peps.pt[i].ctl) == 0) && (i==0 || valid_entry(peps.pt[i-1].ctl,peps.pt[i-1].page))) //If the current entry can be removed and the previous is valid, i is the start of the interval
                {
                    first = i;
                }
                if(first>=0 && (GETREFBIT(peps.pt[i].ctl) == 0 || GETVALBIT(peps.pt[i].ctl) == 0) && i-first==npages-1){ //We found npages contiguous entries that can be removed
                    DEBUG(DB_VM,"Found a space for a kmalloc for process %d entry%d\n",curproc->p_pid,first);
                    for(j=first;j<=i;j++){
                        KASSERT(peps.pt[j].page!=KMALLOC_PAGE);
                        KASSERT(!GETTLBBIT(peps.pt[j].ctl));
                        KASSERT(!GETREFBIT(peps.pt[j].ctl) || !GETVALBIT(peps.pt[j].ctl));
                        KASSERT(!GETIOBIT(peps.pt[j].ctl));
                        KASSERT(!GETSWAPBIT(peps.pt[j].ctl));
                        old_pid = peps.pt[j].pid; //Again due to parallelism we initialize correctly the new values for the entry before the I/O operation, and we save the old ones to perform the store
                        old_v = peps.pt[j].page;
                        peps.pt[j].pid = curproc->p_pid;
                        peps.pt[j].page = KMALLOC_PAGE; //To remember that this page can't be swapped out until when we perform a free
                        old_val=GETVALBIT(peps.pt[j].ctl);
                        peps.pt[j].ctl = VALBITONE(peps.pt[j].ctl); //Set pages as valid
                        if(old_val){ //If the page was valid, we must store it in the swapfile
                            peps.pt[j].ctl = IOBITONE(peps.pt[j].ctl);
                            remove_from_hash(old_v,old_pid);//We remove the entry from the hash table
                            store_swap(old_v,old_pid,j * PAGE_SIZE + peps.firstfreepaddr);
                            peps.pt[j].ctl = IOBITZERO(peps.pt[j].ctl);
                            /*
                             * Here we don't wake up any process. In fact, it's true that we're storing a page but
                             * it's already reserved for the kmalloc operation, so it can't be selected as a victim.
                            */
                        }
                    }
                    peps.contiguous[first]=npages; //We save in position first the number of contiguous pages allocated. It'll be useful while freeing
                    lastIndex = (i + 1) % peps.ptSize; //We update lastIndex for the second chance.
                    return first*PAGE_SIZE + peps.firstfreepaddr;
                }
            }
        }
        lastIndex=0; //When we arrive at the end of the page table we restart. In fact, the pages must be physically contiguous (and not contiguous in the circular buffer of the IPT).

        if(first_iteration<2){ //Also here we perform 2 full iterations to have a full execution of the second chance algorithm
            first_iteration++;
        }
        else{
            lock_acquire(peps.pt_lock);
            cv_wait(peps.pt_cv,peps.pt_lock); //If after 2 full iterations we didn't find a suitable interval we sleep until when something changes
            lock_release(peps.pt_lock);
            first_iteration=0; //To perform again 2 full iterations
        }

        first=-1; //We reset first for the same reason seen for lastIndex
    }

    return ENOMEM; //Useless return (we never arrive here), inserted to avoid compilation errors
}

void free_contiguous_pages(vaddr_t addr){
    
    int i, index, niter;

    paddr_t p = KVADDR_TO_PADDR(addr); //We retrieve the physical address of the starting page

    index = (p - peps.firstfreepaddr) / PAGE_SIZE; //We get the index to use in the IPT
    niter = peps.contiguous[index]; //We access contiguous to get the number of pages to free

    DEBUG(DB_VM,"Process %d performs kfree for %d pages\n", curproc?curproc->p_pid:0,niter);

    #if OPT_DEBUG
    nkmalloc-=niter;
    KASSERT(niter!=-1);
    #endif

    for(i=index;i<index+niter;i++){
        KASSERT(peps.pt[i].page==KMALLOC_PAGE);
        peps.pt[i].ctl = VALBITZERO(peps.pt[i].ctl); //The pages aren't valid anymore
        peps.pt[i].page=0; //We clear the kmalloc flag
    }

    peps.contiguous[index]=-1;

    /**
     * Small trick. Locks can't be acquired if we're in an interrupt handler, which is the case for exorcise. To avoid potential suboptimizations/starvations, if we are in an interrupt handler
     * we don't acquire the lock (the check on lock_do_i_hold has been placed inside an if) and we simply perform cv_broadcast, otherwise we proceed in the standard way.
    */

    if(curthread->t_in_interrupt == false){
        lock_acquire(peps.pt_lock);
        cv_broadcast(peps.pt_cv,peps.pt_lock); //Since we freed some pages, we wake up the processes waiting on the cv.
        lock_release(peps.pt_lock);
    }
    else{
        cv_broadcast(peps.pt_cv,peps.pt_lock);
    }

    #if OPT_DEBUG
    DEBUG(DB_VM,"New kmalloc number after free=%d\n",nkmalloc);
    #endif
}

void copy_pt_entries(pid_t old, pid_t new){

    int pos;

    for(int i=0;i<peps.ptSize;i++){
        if(peps.pt[i].pid==old && GETVALBIT(peps.pt[i].ctl) && peps.pt[i].page!=KMALLOC_PAGE){ //We copy all the valid pages of old, except for kmalloc pages
            pos = findspace();
            if(pos==-1){ //If there isn't any free space we simply copy the page inside the swapfile (to avoid victim selection, which would be potentially unfeasible if we don't have enough space)
                KASSERT(!GETIOBIT(peps.pt[i].ctl));
                KASSERT(GETSWAPBIT(peps.pt[i].ctl));
                KASSERT(peps.pt[i].page!=KMALLOC_PAGE);
                DEBUG(DB_VM,"Copied from pt address 0x%x for process %d\n",peps.pt[i].page,new);
                store_swap(peps.pt[i].page,new,peps.firstfreepaddr+i*PAGE_SIZE); //We save in the swapfile the page, that'll belong to the new pid
            }
            else{ //We found a non valid page, that can be used to store the page
                peps.pt[pos].ctl = VALBITONE(peps.pt[pos].ctl);
                peps.pt[pos].page = peps.pt[i].page;
                peps.pt[pos].pid = new;
                add_in_hash(peps.pt[i].page,new,pos);
                memmove((void *)PADDR_TO_KVADDR(peps.firstfreepaddr + pos*PAGE_SIZE),(void *)PADDR_TO_KVADDR(peps.firstfreepaddr + i*PAGE_SIZE), PAGE_SIZE); //It's a copy within RAM, so we can use memmove. The reason to use PADDR_TO_KVADDR is explained in swapfile.c
                KASSERT(!GETIOBIT(peps.pt[pos].ctl));
                KASSERT(!GETTLBBIT(peps.pt[pos].ctl));
                KASSERT(!GETSWAPBIT(peps.pt[pos].ctl));
                KASSERT(peps.pt[pos].page!=KMALLOC_PAGE);
            }
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
            peps.pt[i].ctl = SWAPBITONE(peps.pt[i].ctl); //To freeze the current situation we set the swap bit to 1. This is done to avoid inconsistencies between the situation at the beginning and at the end of the swapping process.
        }
    }
}

void end_copy_pt(pid_t pid){

    for(int i=0;i<peps.ptSize;i++){
        if(peps.pt[i].pid == pid && peps.pt[i].page!=KMALLOC_PAGE && GETVALBIT(peps.pt[i].ctl)){
            KASSERT(GETSWAPBIT(peps.pt[i].ctl));
            peps.pt[i].ctl = SWAPBITZERO(peps.pt[i].ctl); //We set the swap bit to 1
        }
    }

    lock_acquire(peps.pt_lock);
    cv_broadcast(peps.pt_cv,peps.pt_lock); //Since the pages that were previously involved in the swap can now be selected as victims, we wake up all the processes waiting on the cv
    lock_release(peps.pt_lock);

}

#if OPT_DEBUG
void print_nkmalloc(void){
    kprintf("Final number of kmalloc: %d\n",nkmalloc);
}
#endif