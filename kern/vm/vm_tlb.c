
#include "vm_tlb.h"
#include "mips/tlb.h"
#include "pt.h"
#include "spl.h"
#include "addrspace.h"
#include "opt-dumbvm.h"
#include "opt-test.h"
/*
 * vm.h includes the definition of vm_fault, which is used to handle the
 * TLB misses
 */
#include "vm.h"
#include "vmstats.h"
/*todo: ask the boys what it was meant for because I do not remember anymore*/
int tlb_remove(void){
    return -1;
}


#if !OPT_DUMBVM
int vm_fault(int faulttype, vaddr_t faultaddress){

    // if(faultaddress >= 0x412000)
    //     kprintf("TLB miss for 0x%x\n",faultaddress);

    //print_tlb();

    //kprintf("\nfault address: 0x%x\n",faultaddress);
    paddr_t paddr;
    uint32_t mask = PAGE_FRAME, addr, res;

    addr = (uint32_t) faultaddress;

    res = addr & mask; // do I ?

    /*I update the statistics*/
    add_tlb_fault();
    /*I extract the virtual address of the corresponding page*/
    switch (faulttype)
    {
    case VM_FAULT_READ:
        
        break;
    case VM_FAULT_WRITE:
      
        break;
    case VM_FAULT_READONLY:
        kprintf("You tried to write a readonly segment... The process is ending...");
        sys__exit(0);
        break;
    
    default:
        break;
    }
    /*If I am here is either a VM_FAULT_READ or a VM_FAULT_WRITE*/
    /*did mattia set up the as correctly?*/
    KASSERT(as_is_ok() == 1);

    paddr = get_page(res);
    int spl = splhigh(); // so that the control does nit pass to another waiting process.
    tlb_insert(res, paddr);
    splx(spl);
    return 0;
}
#endif 

int tlb_victim(void){
    /*atm, RR strategy*/
    int victim;
    static unsigned int next_victim = 0;
    victim = next_victim;
    next_victim = (next_victim + 1) % NUM_TLB;
    return victim;   
}

int segment_is_readonly(vaddr_t vaddr){
    struct addrspace *as;
    as = proc_getas();
    int is_ro = 0;
    uint32_t first_text_vaddr = as->as_vbase1;
    int size = as->as_npages1;
    uint32_t last_text_vaddr = (size*PAGE_SIZE) + first_text_vaddr;
    if((vaddr>=first_text_vaddr)  && (vaddr <= last_text_vaddr))
        is_ro = 1;
    return is_ro;

}

int tlb_insert(vaddr_t faultvaddr, paddr_t faultpaddr){
    /*faultpaddr is the address of the beginning of the physical frame, so I have to remember that I do not have to pass the whole address but I have to mask the ls 12 bits*/
    int entry, valid, is_RO; 
    uint32_t hi, lo, prevHi, prevLo;
    is_RO = segment_is_readonly(faultvaddr); // boolean that tells me if the dirty bit has to be (un)set
    
    /*step 1: look for a free entry and update the corresponding statistic (FREE)*/
    for(entry = 0; entry <NUM_TLB; entry++){
        valid = tlb_entry_is_valid(entry);
        if(!valid){
            // if(entry==0){
            //     pt_reset_tlb();
            // }
            /*I can write the fault address here! that's a match,,,*/
                hi = faultvaddr;
                lo = faultpaddr | TLBLO_VALID;
                /*is the segment a text segment?*/
               if(!is_RO){
                    /*I have to set a dirty bit that is basically a write privilege*/
                    lo = lo | TLBLO_DIRTY; 
                }
                tlb_write(hi, lo, entry);
            /*update tlb fault free*/
            add_tlb_type_fault(FAULT_W_FREE); //do I have to add the general faults as well or do I do it earlier?
            /*return*/
            return 0;
        }

    }
    /*step 2: I have not found an invalid entry, so,,, look for a victim, override, update the correspnding statistic (REPLACE)*/
    entry = tlb_victim();
     hi = faultvaddr;
    lo = faultpaddr | TLBLO_VALID;
    /*is the segment a text segment?*/
    if(!is_RO){
        /*I have to set a dirty bit that is basically a write privilege*/
        lo = lo | TLBLO_DIRTY; 
    }
    tlb_read(&prevHi, &prevLo, entry);
    /*notify the pt that the entry with that virtual address is not in tlb anymore*/
    cabodi(prevHi);
    tlb_write(hi, lo, entry);
    /*update tlb faults replace*/
    add_tlb_type_fault(FAULT_W_REPLACE);
    return 0;

}

int tlb_entry_is_valid(int i){
    uint32_t hi, lo;
    tlb_read(&hi, &lo, i);
    /*then extract the validity bit and return the result. if the result is 0 it means that the validity bit is not set and therefore the entry is invalid.*/
    return (lo & TLBLO_VALID);
}

int tlb_invalidate_entry(paddr_t paddr){
    /*look for a match in the TLB*/
    paddr_t frame_addr_stored;
    paddr_t frame_addr = paddr & TLBLO_PPAGE; 
    uint32_t hi, lo;
    for(int i = 0; i<NUM_TLB; i++){
        tlb_read(&hi, &lo, i);
        frame_addr_stored = lo & TLBLO_PPAGE ; // I extract the physical address stored in each entry
        if(frame_addr_stored == frame_addr)
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    return 0;
}

void tlb_invalidate_all(void){
    uint32_t hi, lo;
    add_tlb_invalidation();
    for(int i = 0; i<NUM_TLB; i++){
            if(tlb_entry_is_valid(i)){
                tlb_read(&hi,&lo,i);
                cabodi(hi);
            }
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
}

void print_tlb(void){
    uint32_t hi, lo;

    kprintf("\n\n\tTLB\n\n");

    for(int i = 0; i<NUM_TLB; i++){
        tlb_read(&hi, &lo, i);
        kprintf("%d virtual: 0x%x, physical: 0x%x\n", i, hi, lo);
    }

}

