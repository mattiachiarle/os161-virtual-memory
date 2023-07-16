
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



/*not needed anymore, we leave it here in case we want it to be a wrapper for the mips instruction TLB_INVALIDATE*/
int tlb_remove(void){
    return -1;
}


#if OPT_PROJECT
int vm_fault(int faulttype, vaddr_t faultaddress){

    #if OPT_DEBUG
    print_tlb();
    #endif

    DEBUG(DB_VM,"\nfault address: 0x%x\n",faultaddress);
    int spl = splhigh(); // so that the control does not pass to another waiting process.
    paddr_t paddr;
  
    faultaddress &= PAGE_FRAME; // I extract the address of the frame that caused the fault (it was not in the TLB)

    /*I update the statistics*/
    add_tlb_fault();
    /*I extract the virtual address of the corresponding page*/
    switch (faulttype)
    {
    case VM_FAULT_READ:
        
        break;
    case VM_FAULT_WRITE:
      
        break;
        /*The readonly case hase to be cosidered special: the text segment cannot be written by the process. 
        Therefore, if the process tries to modify a RO segment, the process has to be ended by means of the 
        appropriate system call (no need to panic)*/
    case VM_FAULT_READONLY:
        kprintf("You tried to write a readonly segment... The process is ending...");
        sys__exit(0);
        break;
    
    default:
        break;
    }
    /*If I am here is either a VM_FAULT_READ or a VM_FAULT_WRITE*/
    /*was the address space set up correctly?*/
    KASSERT(as_is_ok() == 1);
   /*If the address space was set up correctly, I ask the Page table for the virtual address address of the frame that is not present in the TLB*/
    paddr = get_page(faultaddress);
    /*Now that I have the address, I can insert it into the TLB */
    tlb_insert(faultaddress, paddr);
    splx(spl);
    return 0;
}
#endif 

/**
 * This function chooses the entry to sacrifice in the TLB and returns its index.
*/
int tlb_victim(void){
    /*I chose the entry to invalidate by means of a RR strategy*/
    int victim;
    static unsigned int next_victim = 0;
    victim = next_victim;
    next_victim = (next_victim + 1) % NUM_TLB;
    return victim;   
}

/**
 * This function determines if the frame is readonly
*/
int segment_is_readonly(vaddr_t vaddr){
    struct addrspace *as;
    as = proc_getas(); // I get the address space of the process
    int is_ro = 0;
    uint32_t first_text_vaddr = as->as_vbase1;
    int size = as->as_npages1;
    uint32_t last_text_vaddr = (size*PAGE_SIZE) + first_text_vaddr;
    /*if the virtual address is in the range of virtual addresses assigned to the text segment, I set is_ro to true*/
    if((vaddr>=first_text_vaddr)  && (vaddr <= last_text_vaddr))
        is_ro = 1;
    return is_ro;

}

/**
 * This function writes a new entry in the TLB. It receives as parameters the fault address (virtual) and the 
 * corresponding physical one received by the page table. 
 * It finds a space to insert the entry (vaddr, paddr) by means of the tlb_victim.
*/
int tlb_insert(vaddr_t faultvaddr, paddr_t faultpaddr){
    /*faultpaddr is the address of the beginning of the physical frame, so I have to remember that I do not have to 
    pass the whole address but I have to mask the least significant 12 bits*/
    int entry, valid, is_RO; 
    uint32_t hi, lo, prevHi, prevLo;
    is_RO = segment_is_readonly(faultvaddr); // boolean that tells me if the address is read_only and therefore the dirty bit has to be set
    
    /*step 1: look for a free entry and update the corresponding statistic (FREE)*/
    for(entry = 0; entry <NUM_TLB; entry++){
        valid = tlb_entry_is_valid(entry);
        if(!valid){
            /*I can write the fault address here!*/
                hi = faultvaddr;
                lo = faultpaddr | TLBLO_VALID;
                /*is the segment a text segment?*/
               if(!is_RO){
                    /*I have to set a dirty bit (that is basically a write privilege)*/
                    lo = lo | TLBLO_DIRTY; 
                }
                tlb_write(hi, lo, entry);
            /*update the statistic "tlb fault free"*/
            add_tlb_type_fault(FAULT_W_FREE); //do I have to add the general faults as well or do I do it earlier?
            /*return*/
            return 0;
        }

    }
    /*step 2: I have not found an invalid entry. so... look for a victim, overwrite and update the correspnding statistic (REPLACE)*/
    entry = tlb_victim();
    hi = faultvaddr;
    lo = faultpaddr | TLBLO_VALID; //the entry has to be set as valid
    /*is the segment a text segment?*/
    if(!is_RO){
        /*I have to set a dirty bit that is basically a write privilege*/
        lo = lo | TLBLO_DIRTY; 
    }
    /*before overwriting the entry, I have to save the current content somewhere so that I can notify the page table of the replacement. The page
    table, indeed, keeps trace of its entries that are cached (in the TLB)*/
    tlb_read(&prevHi, &prevLo, entry);
    /*notify the pt that the entry with that virtual address is not in tlb anymore*/
    update_tlb_bit(prevHi, curproc->p_pid);
    /*Now I can overwrite the content*/
    tlb_write(hi, lo, entry);
    /*update tlb faults replace*/
    add_tlb_type_fault(FAULT_W_REPLACE);
    return 0;

}

/**
 * This function tells me if the entry in the TLB at index i is valid or not.
*/
int tlb_entry_is_valid(int i){
    uint32_t hi, lo;
    /*I read the content of the entry at index i*/
    tlb_read(&hi, &lo, i);
    /*then extract the validity bit and return the result. if the result is 0 it means that the validity bit is not set and therefore the entry is invalid.*/
    return (lo & TLBLO_VALID);
}

/**
 * this function inavlidates the entry corresponding to the given physical address.
 * It is useful when an entry in the PT is invalidated and the change has to be propagated to the TLB.
 * @return: 0 if ok
*/
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

/**
 * this function has to invalidate the TLB when there is a switch from a process to another. Indeed, 
 * the TLB is common for all processes and does not have a "pid" field.
*/
void tlb_invalidate_all(void){
    uint32_t hi, lo;
    pid_t pid = curproc->p_pid; // I extract the pid of the currently running process
    if(previous_pid != pid) // the process (not the thread) changed. This is necessary because as_activate is called also when the thread changes.
    {
    DEBUG(DB_VM,"NEW PROCESS RUNNING: %d INSTEAD OF %d\n",pid,previous_pid);

    /*I update the correct statistics*/
    add_tlb_invalidation();

    /*I iterate on all the entries*/
    for(int i = 0; i<NUM_TLB; i++){
            if(tlb_entry_is_valid(i)){ // If the entry is valid
                tlb_read(&hi,&lo,i); // retrieve the content
                update_tlb_bit(hi,previous_pid); // I inform the Page Table that the entry identified by the pair (vaddr, pid) will not be "cached" anymore
            }
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i); // I override the entry
            }
    previous_pid = pid; // I update the global variable previous_pid so that the next time that the function is called I can determine if the process has changed.
    }
}
/**
 * Useful for debugging reasons eheh :^)
*/
void print_tlb(void){
    uint32_t hi, lo;

    kprintf("\n\n\tTLB\n\n");

    for(int i = 0; i<NUM_TLB; i++){
        tlb_read(&hi, &lo, i);
        kprintf("%d virtual: 0x%x, physical: 0x%x\n", i, hi, lo);
    }

}

