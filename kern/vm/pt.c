#define VALBITZERO(a) (a & ~1)
#define VALBITONE(a) (a | 1)
#define GETVALBIT(a) (a & 1)
#define REFBITONE(a) (a | 2)
#define REFBITZERO(a) (a & ~1)
#define GETREFBIT(a) (a & 2)
#define TLBBITONE(a) (a | 4)
#define TLBBITZERO(a) (a & ~4)
#define GETTLBBIT(a) (a & 4)

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
    int numFrames;
    // modify in order to take only locatable free space
    numFrames = mainbus_ramsize() / PAGE_SIZE; // get how many frames I have in RAM, remember: 1 IPT entry for each frame
    // the next one: how many entries in a frame, that is a ceil
    // the next one: number of frames for the page table = num of frames / how many entries in a frame, with ceil
    // allocate IPT in kern
    peps.pt = kmalloc(sizeof(struct pt_entry) * numFrames);
    if (peps.pt == NULL) //
    {
        panic("error allocating IPT!!");
    }
    for (int i = 0; i < numFrames; i++) // all validity bit to 0
    {
        peps.pt[i].ctl = VALBITZERO(peps.pt[i].ctl);
    }
    peps.ptSize = numFrames;
    peps.firstfreepaddr = ram_stealmem(0);
    peps.ptlock = lock_create("pagetable-lock");
    if(peps.ptlock==NULL){
        panic("error!! lock not initialized...");
    }
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
paddr_t find_victim(void)
{
    int i;
    // if I am here there will be a replacement since all pages are valid
    for (i = lastIndex;; i = (i + 1) % peps.ptSize)
    {
        // Val, Ref,TLB bits from low to high
        if (GETTLBBIT(peps.pt[i].ctl) == 0) // if isInTLB = 0
        {
            if (GETREFBIT(peps.pt[i].ctl) == 0) // if Ref bit==0 victim found
            {
                peps.pt[i].ctl = TLBBITONE(peps.pt[i].ctl);  // set isINTLB to 1
                lastIndex = i + 1;                          // new ptr for FIFO
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

paddr_t get_page(vaddr_t v)
{

    pid_t pid = proc_getpid(curproc); // get curpid here
    int res;
    paddr_t pp;
    lock_acquire(peps.ptlock);
    res = pt_get_paddr(v, pid);

    if (res != -1)
    {
        pp = (paddr_t) res;
        add_tlb_reload();
        lock_release(peps.ptlock);
        return pp;
    }
    int pos = findspace(); // find a free space
    if (pos == -1)
    {
        pp = find_victim();
    }
    else{
        pp = peps.firstfreepaddr + pos*PAGE_SIZE;
    }

    load_page(v, pid, pp);
    lock_release(peps.ptlock);
    return pp;
}

int pt_get_paddr(vaddr_t v, pid_t p)
{
    int validity;

    for (int i = 0; i < peps.ptSize; i++)
    {
        validity = GETVALBIT(peps.pt[i].ctl);
        if (v == peps.pt[i].page && peps.pt[i].pid == p && validity)
        {
            peps.pt[i].ctl = REFBITONE(peps.pt[i].ctl); // set RB to 1, that is the second ctl bit
            peps.pt[i].ctl = TLBBITONE(peps.pt[i].ctl); // set isInTLB to 1, third ctl bit
            return i * PAGE_SIZE + peps.firstfreepaddr; // send the paddr found
        }
    }
    // else insert in IPT the right varrd+pid from Mattia el Chiurlo

    // panic("Physical address not found! It's impossible right??");
    return -1; // not found in PT in future PANIC!!
}

void free_pages(pid_t p)
{
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
        if (peps.pt[i].page == v && peps.pt[i].pid==pid)
        {
            KASSERT((peps.pt[i].ctl & 4));               // it must be inside TLB
            peps.pt[i].ctl = TLBBITZERO(peps.pt[i].ctl); // remove TLB bit
            peps.pt[i].ctl = REFBITONE(peps.pt[i].ctl);  // set RB to 1
            return 1;                                    //
        }
    }
    return -1;
}
