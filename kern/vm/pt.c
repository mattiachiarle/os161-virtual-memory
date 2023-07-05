#include "coremap.h"
#include "vm.h"
#include "mainbus.h"
#include "lib.h"
#include "cpu.h"
#include "types.h"
#include "spinlock.h"
#include "pt.h"

int lastIndex = 0; // for FIFO, need to check priorities: Ref. bit and TLB bit
// for now idea: to get first addr use ram_stealmem or put visible firstaddr

void pt_init(void)
{
    int numFrames, entInFrame, framesForPT;

    // modify in order to take only locatable free space
    numFrames = mainbus_ramsize() / PAGE_SIZE; // get how many frames I have in RAM, remember: 1 IPT entry for each frame
    // the next one: how many entries in a frame, that is a ceil
    // the next one: number of frames for the page table = num of frames / how many entries in a frame, with ceil
    // allocate IPT in kern
    tommaso.pt = kmalloc(sizeof(struct pt_entry) * numFrames);
    if (tommaso.pt == NULL) //
    {
        panic("error allocating IPT");
    }
    for (int i = 0; i < numFrames; i++) // all validity bit to 0
    {
        tommaso.pt[i].ctl = tommaso.pt[i].ctl & ~1;
    }
    tommaso.ptSize = numFrames;
    tommaso.firstfreepaddr = ram_stealmem(0);
}

// some bits for control; from the lower:  Validity bit, Reference bit, isInTLB bit, ...
//  bitStatus = (j >> n) & 1;

paddr_t get_page(vaddr_t v)
{
    pid_t pid; // get curpid here
    paddr_t pp;
    pp = pt_get_paddr(v, pid);

    if (pp != -1)
    {
        return pp;
    }
    int pos = findspace(); // find a free space
    if (pos == -1)
    {
        // find victim
    }

    // MATTIA LOAD_PAGE( v, pid, pos*PAGE_SIZE );
}

int findspace()
{
    int validity = -1;
    for (int i = 0; i < tommaso.ptSize; i++)
    {
        validity = tommaso.pt[i].ctl & 1; // 1 if validity bit=1
        if (!validity)
        {
            return i; // return the position of empty entry in PT
        }
    }
    return -1;
}

paddr_t pt_get_paddr(vaddr_t v, pid_t p)
{
    int validity;

    for (int i = 0; i < tommaso.ptSize; i++)
    {
        validity = tommaso.pt[i].ctl & 1;
        if (v == tommaso.pt[i].page && tommaso.pt[i].pid == p && validity)
        {
            tommaso.pt[i].ctl = tommaso.pt[i].ctl | 2;     // set RB to 1, that is the second ctl bit
            tommaso.pt[i].ctl = tommaso.pt[i].ctl | 4;     // set isInTLB to 1, third ctl bit
            return i * PAGE_SIZE + tommaso.firstfreepaddr; // send the paddr found
        }
    }
    // else insert in IPT the right varrd+pid from Mattia el Chiurlo

    return -1; // not found in PT in future PANIC!!
}

// name is wrong --> select victim
paddr_t find_victim()
{
    int i;
    // if I am here there will be a replacement since all pages are valid
    for (i = lastIndex;; i = (i + 1) % tommaso.ptSize)
    {
        // Val, Ref,TLB bits from low to high
        if ((tommaso.pt[i].ctl & 4) == 0) // if isInTLB = 0
        {
            if (tommaso.pt[i].ctl & 2 == 0) // if Ref bit==0 victim found
            {
                tommaso.pt[i].ctl |= 4;                        // set isINTLB to 1
                lastIndex = i + 1;                             // new ptr for FIFO
                return i * PAGE_SIZE + tommaso.firstfreepaddr; // return paddr of that frame
            }
            else
            {                                               // RB=1
                tommaso.pt[i].ctl = tommaso.pt[i].ctl & ~2; // set RB to 0 and continue
            }
        }
    }
}

// remove old one from TLB and insert the new one
// Put in swapfile old pt[i].v and old pt[i].v
// pt[i].ctl |= 4; // set isInTLB bit
// pt[i].page = v; // update vaddr and pageid inside page table
// pt[i].page = p;
// lastIndex = i; // update last index for FIFO
// return i * PAGE_SIZE /* + firstpaddr */;

int free_pages(pid_t p)
{

    for (int i = 0; i < tommaso.ptSize; i++)
    {
        if (tommaso.pt[i].pid == p && (tommaso.pt[i].ctl | 1) == 1) // if it is valid and it is the same pid passed
        {
            tommaso.pt[i].ctl &= 0; // set all controls to 0
        }
    }

    // free pages also in swapfile
}

int cabodi(vaddr_t v)
{
    pid_t pid; // take pid from curproc
    int i;
    for (i = 0; i < tommaso.ptSize; i++)
    {
        if (tommaso.pt[i].page == v)
        {
            KASSERT( (tommaso.pt[i].ctl & 4 ) );  //it must be inside TLB
            tommaso.pt[i].ctl = tommaso.pt[i].ctl & ~ 4; //remove TLB bit
            tommaso.pt[i].ctl |= 2;  // set RB to 1
            return 1; // 
        }
    }
    return -1;
}
