#include "vm_tlb.h"
#include "tlb.h"
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

int vm_fault(int faulttype, vaddr_t faultaddress){
    return -1;
}

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
    uint32_t hi, lo;
    is_RO = segment_is_readonly(faultvaddr); // boolean that tells me if the dirty bit has to be (un)set
    
    /*step 1: look for a free entry and update the corresponding statistic (FREE)*/
    for(entry = 0; entry <NUM_TLB; entry++){
        valid = tlb_entry_is_valid(entry);
        if(!valid){
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
    /*step 2: I have not found an invalid table, so,,, look for a victim, override, update the correspnding statistic (REPLACE)*/
    entry = tlb_victim();
     hi = faultvaddr;
    lo = faultpaddr | TLBLO_VALID;
    /*is the segment a text segment?*/
    if(!is_RO){
        /*I have to set a dirty bit that is basically a write privilege*/
        lo = lo | TLBLO_DIRTY; 
    }
    tlb_write(hi, lo, entry);
    /*update tlb faults replace*/
    add_pt_type_fault(FAULT_W_REPLACE);
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
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID());
    }
    return 0;
}