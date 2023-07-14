#define VALBITZERO(a) (a & ~1)
#define VALBITONE(a) (a | 1)
#define GETVALBIT(a) (a & 1)
#define REFBITONE(a) (a | 2)
#define REFBITZERO(a) (a & ~2)
#define GETREFBIT(a) (a & 2)
#define TLBBITONE(a) (a | 4)
#define TLBBITZERO(a) (a & ~4)
#define GETTLBBIT(a) (a & 4)

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

    // This numframes will be higher than the actual number of frames available since it doesn't take into account the kmalloc.
    // However this won't cause errors since ptsize will be correctly initialized.
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

    peps.ptlock = lock_create("pagetable-lock");
    if (peps.ptlock == NULL)
    {
        panic("error!! lock not initialized...");
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
        peps.pt[i].ctl = VALBITZERO(peps.pt[i].ctl);
        peps.contiguous[i] = -1;
    }

    peps.firstfreepaddr = ram_stealmem(0);
    // kprintf("\nRam size :0x%x, first free address: 0x%x, available memory: 0x%x",mainbus_ramsize(),ram_stealmem(0),mainbus_ramsize()-ram_stealmem(0));
    //  modify in order to take only locatable free space
    peps.ptSize = ((mainbus_ramsize() - ram_stealmem(0)) / PAGE_SIZE) - 1;

    // kprintf("\n\n%d\n\n", peps.ptSize*1.3);
    int part = 1.3;                                                   // peps.ptSize * 2;
    htable.size = (int)part * peps.ptSize;                            // size of hash table
    htable.table = kmalloc(sizeof(struct hashentry *) * htable.size); // alloc hash table
    for (int ii = 0; ii < htable.size; ii++)
    {
        htable.table[ii] = NULL; // no pointers for now
    }

    unusedptrlist = NULL;
    struct hashentry *tmp;
    for (int jj = 0; jj < /*htable.size*/ peps.ptSize; jj++) // initialize unused ptr list, must be
    {
        tmp = kmalloc(sizeof(struct hashentry));
        if (!tmp)
        {
            panic("Error during hashpt elements allocation");
        }
        tmp->next = unusedptrlist;
        unusedptrlist = tmp;
    }

    pt_active = 1;

    spinlock_release(&stealmem_lock);
}

static int findspace()
{
    int val = -1;
    for (int i = 0; i < peps.ptSize; i++)
    {
        val = GETVALBIT(peps.pt[i].ctl); // 1 if validity bit=1
        if (!val)
        {
            return i; // return the position of empty entry in PT
        }
    }
    return -1;
}

// name is wrong --> select victim
paddr_t find_victim(vaddr_t vaddr, pid_t pid)
{
    int i;
    // if I am here there will be a replacement since all pages are valid
    for (i = lastIndex;; i = (i + 1) % peps.ptSize)
    {
        // Val, Ref,TLB bits from low to high
        if (peps.pt[i].page != KMALLOC_PAGE && GETTLBBIT(peps.pt[i].ctl) == 0) // if isInTLB = 0
        {
            if (GETREFBIT(peps.pt[i].ctl) == 0) // if Ref bit==0 victim found
            {
                lock_release(peps.ptlock);
                store_swap(peps.pt[i].page, peps.pt[i].pid, i * PAGE_SIZE + peps.firstfreepaddr);
                lock_acquire(peps.ptlock);
                retrieve_from_hash(peps.pt[i].page, peps.pt[i].pid); // ADDED NEW
                peps.pt[i].ctl = TLBBITONE(peps.pt[i].ctl);          // set isINTLB to 1
                peps.pt[i].page = vaddr;
                peps.pt[i].pid = pid;
                lastIndex = (i + 1) % peps.ptSize;          // new ptr for FIFO
                add_in_hash(vaddr, pid, i);                 // ADDED NEW
                return i * PAGE_SIZE + peps.firstfreepaddr; // return paddr of that frame
            }
            else
            {                                                // found rb==1, so-->
                peps.pt[i].ctl = REFBITZERO(peps.pt[i].ctl); // set RB to 0 and continue
            }
        }
    }
    panic("no victims! it's a problem...");
}

// some bits for control; from the lower:  Validity bit, Reference bit, isInTLB bit, ...
//  bitStatus = (j >> n) & 1;

int get_hash_func(vaddr_t v, pid_t p)
{
    // int val = (((((int)v) % 24) + (((int)p % 8) << 24)) ^ 1234567891) % htable.size;
    int val = (((int)v) % 24) + ((((int)p) % 8) << 24);
    val = val ^1234567891;
    val = val % htable.size;
    return val;
}

void add_in_hash(vaddr_t vad, pid_t pid, int pos) // NEW
{
    int val = get_hash_func(vad, pid);
    struct hashentry *tmp = unusedptrlist; // take the first from the free
    unusedptrlist = tmp->next;
    tmp->next = htable.table[val]; // insert in hashtable
    htable.table[val] = tmp;       // attach in head here
    tmp->vad = vad;
    tmp->pid = pid; // update values
    tmp->iptentry = pos;
}

int get_index_from_hash(vaddr_t vad, pid_t pid) // NEW
{
    int val = get_hash_func(vad, pid);
    struct hashentry *tmp = htable.table[val];
    while (tmp != NULL)
    {
        if (tmp->vad == vad && tmp->pid == pid)
        {
            // peps.pt[val].ctl = REFBITONE(peps.pt[val].ctl); // set RB to 1, that is the second ctl bit
            // peps.pt[val].ctl = TLBBITONE(peps.pt[val].ctl); // set isInTLB to 1, third ctl bit
            return tmp->iptentry;
        }
        tmp = tmp->next;
    }
    return -1;
}

void retrieve_from_hash(vaddr_t vad, pid_t pid) // NEW
{
    int val = get_hash_func(vad, pid);

    struct hashentry *tmp = htable.table[val];
    struct hashentry *tmpnext;
    if (tmp == NULL) // means that is empty, strange..
    {
        return;
    }

    if (tmp->vad == vad && tmp->pid == pid) // if found in head remove instantly
    {
        htable.table[val] = tmp->next; /// take second in list
        tmp->next = unusedptrlist;
        unusedptrlist = tmp;
        return;
    }

    while (tmp != NULL) // start pointing first element
    {
        tmpnext = tmp->next;
        if (tmpnext->vad == vad && tmpnext->pid == pid) // if found
        {
            tmp->next = tmpnext->next; // remove from the list
            tmpnext->next = unusedptrlist;
            unusedptrlist = tmpnext; // update in head list of unused ptrs
            // unusedptrlist->vad=NULL;
            // unusedptrlist->pid=NULL;   maybe not necessary

            return;
        }
        tmp = tmpnext; // update
    }

    panic("nothing to remove found!!");
}

paddr_t get_page(vaddr_t v)
{

    pid_t pid = proc_getpid(curproc); // get curpid here
    int res;
    paddr_t pp;
    lock_acquire(peps.ptlock);
    res = pt_get_paddr(v, pid);

    if (res != -1)
    {

        pp = (paddr_t)res;
        add_tlb_reload();
        lock_release(peps.ptlock);
        return pp;
    }
    int pos = findspace(v, pid); // find a free space
    if (pos == -1)
    {
        pp = find_victim(v, pid);
    }
    else
    {
        add_in_hash(v, pid, pos); // NEW
        pp = peps.firstfreepaddr + pos * PAGE_SIZE;
        peps.pt[pos].ctl = VALBITONE(peps.pt[pos].ctl);
        peps.pt[pos].ctl = TLBBITONE(peps.pt[pos].ctl);
        peps.pt[pos].page = v;
        peps.pt[pos].pid = pid;
    }

    lock_release(peps.ptlock);
    load_page(v, pid, pp);
    lock_acquire(peps.ptlock);

    lock_release(peps.ptlock);

    return pp;
}

int pt_get_paddr(vaddr_t v, pid_t p)
{

    int x = get_index_from_hash(v, p);
    if (x != -1)
    {
        peps.pt[x].ctl = REFBITONE(peps.pt[x].ctl); // set RB to 1, that is the second ctl bit
        peps.pt[x].ctl = TLBBITONE(peps.pt[x].ctl); // set isInTLB to 1, third ctl bit
        return x * PAGE_SIZE + peps.firstfreepaddr;
    }
    else
    {
        return -1;
    }
    return -1;

    // maybe will be old part

    /*
    int validity;
    for (int i = 0; i < peps.ptSize; i++)
    {
        validity = GETVALBIT(peps.pt[i].ctl);
        if (v == peps.pt[i].page && peps.pt[i].pid == p && validity)
        {
            peps.pt[i].ctl = REFBITONE(peps.pt[i].ctl); // set RB to 1, that is the second ctl bit
            peps.pt[i].ctl = TLBBITONE(peps.pt[i].ctl); // set isInTLB to 1, third ctl bit
            // kprintf("\n %d -- ", get_index_from_hash(v, p));
            // kprintf(" %d\n", i);

            return i * PAGE_SIZE + peps.firstfreepaddr; // send the paddr found
        }
    } */
    // else insert in IPT the right varrd+pid from Mattia el Chiurlo

    // panic("Physical address not found! It's impossible right??");
    return -1; // not found in PT in future PANIC!!
}

int free_hash(struct hashentry **pe, pid_t pid) // NEW
{

    struct hashentry *tmp = *pe;
    struct hashentry *tmpnext;
    if (tmp == NULL) // means that is empty
    {
        return -1;
    }
    if (tmp->pid == pid) // if found in head remove instantly
    {
        *pe = tmp->next; /// take second in list
        tmp->next = unusedptrlist;
        unusedptrlist = tmp;
        return 1;
    }
    while (tmp != NULL) // start pointing first element
    {
        tmpnext = tmp->next;
        if (tmpnext->pid == pid) // if found
        {
            tmp->next = tmpnext->next; // remove from the list
            tmpnext->next = unusedptrlist;
            unusedptrlist = tmpnext; // update in head list of unused ptrs
            return 1;
        }
        tmp = tmpnext; // update
    }
    return -1;
}

void free_pages(pid_t p)
{
    for (int ii = 0; ii < htable.size; ii++)
    {
        while (1)
        {
            int x = free_hash(&htable.table[ii], p);
            if (x == -1)
                break;
        }
    }

    for (int i = 0; i < peps.ptSize; i++)
    {
        if (peps.pt[i].pid == p && GETVALBIT(peps.pt[i].ctl)) // if it is valid and it is the same pid passed
        {
            peps.pt[i].ctl = VALBITZERO(peps.pt[i].ctl); // set valbit to 0
        }
    }
    // free pages also in swapfile
}

int cabodi(vaddr_t v)
{
    pid_t pid = proc_getpid(curproc); // take pid from curproc
    int i;
    for (i = 0; i < peps.ptSize; i++)
    {
        if (peps.pt[i].page == v && peps.pt[i].pid == pid)
        {
            KASSERT((peps.pt[i].ctl & 4));               // it must be inside TLB
            peps.pt[i].ctl = TLBBITZERO(peps.pt[i].ctl); // remove TLB bit
            peps.pt[i].ctl = REFBITONE(peps.pt[i].ctl);  // set RB to 1
            return 1;                                    //
        }
    }
    return -1;
}

paddr_t get_contiguous_pages(int npages)
{
    int i, j, first = -1, valid, prev = 0;

    if (npages > peps.ptSize)
    {
        panic("Not enough memory for kmalloc");
    }

    for (i = 0; i < peps.ptSize; i++)
    {
        valid = GETVALBIT(peps.pt[i].ctl);
        if (i != 0)
        {
            prev = GETVALBIT(peps.pt[i - 1].ctl);
        }
        if (!valid && (i == 0 || prev))
        {
            first = i;
        }
        if (!valid && i - first == npages - 1)
        {
            for (j = first; j <= i; j++)
            {
                peps.pt[j].ctl = VALBITONE(j); // Set pages as valid
                peps.pt[j].page = KMALLOC_PAGE;
                // vaddr and pid are useless here since kernel uses a different address translation
            }
            peps.contiguous[first] = npages;
            return first * PAGE_SIZE + peps.firstfreepaddr;
        }
    }

    // MISSING: KMALLOC WITH FULL PAGE TABLE

    for (i = lastIndex;; i = (i + 1) % peps.ptSize)
    {
        // Val, Ref,TLB bits from low to high
        if (peps.pt[i].page != KMALLOC_PAGE && GETTLBBIT(peps.pt[i].ctl) == 0) // if isInTLB = 0
        {
            if ((GETREFBIT(peps.pt[i].ctl) == 0 || GETVALBIT(peps.pt[i].ctl) == 0) && (i == 0 || (GETREFBIT(peps.pt[i - 1].ctl) != 0 && GETVALBIT(peps.pt[i - 1].ctl) != 0)))
            {
                first = i;
            }
            if ((GETREFBIT(peps.pt[i].ctl) == 0 || GETVALBIT(peps.pt[i].ctl) == 0) && i - first == npages - 1)
            {
                for (j = first; j <= i; j++)
                {
                    if (GETVALBIT(peps.pt[i].ctl) == 1)
                    {
                        lock_release(peps.ptlock);
                        store_swap(peps.pt[i].page, peps.pt[i].pid, i * PAGE_SIZE + peps.firstfreepaddr);
                        lock_acquire(peps.ptlock);
                    }
                    peps.pt[j].ctl = VALBITONE(j); // Set pages as valid
                    peps.pt[j].page = KMALLOC_PAGE;
                    lastIndex = (i + 1) % peps.ptSize;
                }
                peps.contiguous[first] = npages;
                return first * PAGE_SIZE + peps.firstfreepaddr;
            }
            if (GETREFBIT(peps.pt[i].ctl) == 1 && GETVALBIT(peps.pt[i].ctl) == 1)
            {
                peps.pt[i].ctl = REFBITZERO(peps.pt[i].ctl);
            }
        }
    }

    return ENOMEM;
}

void free_contiguous_pages(vaddr_t addr)
{

    int i, index, niter;

    paddr_t p = KVADDR_TO_PADDR(addr);

    index = (p - peps.firstfreepaddr) / PAGE_SIZE;
    niter = peps.contiguous[index];

    for (i = index; i < index + niter; i++)
    {
        peps.pt[i].ctl = VALBITZERO(i);
        peps.pt[i].page = 0;
    }

    peps.contiguous[index] = -1;
}

void pt_reset_tlb(void)
{

    for (int i = 0; i < peps.ptSize; i++)
    {
        if (GETTLBBIT(peps.pt[i].ctl))
        {
            peps.pt[i].ctl = TLBBITZERO(peps.pt[i].ctl);
            peps.pt[i].ctl = REFBITONE(peps.pt[i].ctl);
        }
    }
}
