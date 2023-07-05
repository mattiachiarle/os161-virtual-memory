#include "coremap.h"
#include "vm.h"
#include "mainbus.h"
#include "lib.h"
#include "cpu.h"
#include "types.h"
#include "spinlock.h"
#include "pt.h"

int lastIndex = 0; // for FIFO, need to check priorities: Ref. bit and TLB bit

void pt_init(void)
{
    int numFrames, entInFrame, framesForPT;

    // modify in order to take only locatable free space
    numFrames = mainbus_ramsize() / PAGE_SIZE; // get how many frames I have in RAM, remember: 1 IPT entry for each frame
    // the next one: how many entries in a frame, that is a ceil
    entInFrame = PAGE_SIZE / sizeof(struct pt_entry) + ((PAGE_SIZE % sizeof(struct pt_entry)) != 0);
    // the next one: number of frames for the page table = num of frames / how many entries in a frame, with ceil
    framesForPT = numFrames / (entInFrame) + ((numFrames % sizeof(struct pt_entry)) != 0);
    // allocate IPT in kern
    pt = kmalloc(sizeof(struct pt_entry) * numFrames);
    if (pt == NULL) //
    {
        panic("error allocating IPT");
    }
    for (int i = 0; i < numFrames; i++)
    {
        pt[i].ctl = pt[i].ctl & ~1;
    }
    ptSize = numFrames;
}

// some bits for control; from the lower:  Validity bit, Reference bit, isInTLB bit, ...
//  bitStatus = (j >> n) & 1;

paddr_t pt_get_paddr(vaddr_t v, pid_t p)
{
    int validity;

    for (int i = 0; i < ptSize; i++)
    {
        validity = pt[i].ctl & 1;
        if (validity && v == pt[i].page && pt[i].pid == p)
        {
            pt[i].ctl = pt[i].ctl | 2; // set RB to 1, that is the second ctl bit
            // insert in TLB DONT KNOW HOW
            pt[i].ctl = pt[i].ctl | 4;              // set isInTLB to 1, third ctl bit
            return i * PAGE_SIZE /*+ firstpaddr */; // send the paddr found
        }
    }

    return NULL; // not found in PT
}

paddr_t load_page(vaddr_t v, pid_t p)
{
    KASSERT(is_bitmap_active);

    int i;
    for (i = 0; i < lastIndex + 1; i = (i + 1) % ptSize)
    {
        if ((pt[i].ctl & 1) == 0) // if the entry is invalid I can use it
        {
            pt[i].ctl |= 1; // set Validity bit to 1
            //
            // insert in TLB
            //
            pt[i].ctl |= 4; // set isInTLB bit
            pt[i].page = v; // update vaddr and pageid inside page table
            pt[i].page = p;
            // if present in swapfile remove it from there
            lastIndex = i; // update last index for FIFO
            return i * PAGE_SIZE /* + firstpaddr */;
        }
    }
    // if I am here there will be a replacement since all pages are valid

    for (i = lastIndex; i < lastIndex + 1; i = (i + 1) % ptSize)
    {
        if ((pt[i].ctl & 4) == 0) // if RB = 0
        {
            // remove old one from TLB and insert the new one
            // Put in swapfile old pt[i].v and old pt[i].v
            pt[i].ctl |= 4; // set isInTLB bit
            pt[i].page = v; // update vaddr and pageid inside page table
            pt[i].page = p;
            lastIndex = i; // update last index for FIFO
            return i * PAGE_SIZE /* + firstpaddr */;
        }
    }

    for (i = lastIndex; i < lastIndex + 1; i = (i + 1) % ptSize)
    {
        if ((pt[i].ctl & 2) == 0) // if RB = 0
        {
            pt[i].ctl |= 2; // set RB bit to 1
            //
            // remove the old one from TLB and insert the new one
            //
            // Put in swapfile old pt[i].v and old pt[i].v
            pt[i].ctl |= 4; // set isInTLB bit
            pt[i].page = v; // update vaddr and pageid inside page table
            pt[i].page = p;
            lastIndex = i; // update last index for FIFO
            return i * PAGE_SIZE /* + firstpaddr */;
        }
        else
        {
            pt[i].ctl = pt[lastIndex].ctl & ~2; // set RB to 0 if not here
        }
    }
}

int free_pages(pid_t p)
{

    for (int i = 0; i < ptSize; i++)
    {
        if (pt[i].pid == p && (pt[i].ctl | 1) == 1) // if it is valid and it is the same pid passed
        {
            pt[i].ctl &= 0;  //set validity bit = 0
        }
    }

    //free pages also in swapfile

}
