#include "vmstats.h"
void init_stat(void){
    spinlock_init(&stat.lock);

    stat.tlb_faults=0;
    stat.tlb_free_faults=0;
    stat.tlb_replace_faults = 0;
    stat.tlb_invalidations=0;
    stat.tlb_reloads=0;

    stat.pt_zeroed_faults=0;
    stat.pt_disk_faults=0;
    stat.pt_elf_faults=0;
    stat.pt_swapfile_faults=0;
    /*It might be necessary to insert additional initializations, for "temporary results"*/
}

uint32_t tlb_fault_stats(void){
    //spinlock_acquire(&stat.lock);
    uint32_t s =  stat.tlb_faults;
    //spinlock_release(&stat.lock);
    return s;
}
uint32_t tlb_type_fault_stats(int type){
    //spinlock_acquire(&stat.lock);
    uint32_t s=0;
    switch (type)
    {
    case FAULT_W_FREE:
      s =  stat.tlb_free_faults;
      break;
    case FAULT_W_REPLACE:
        s = stat.tlb_replace_faults;
        break;
    default:
        break;
    }
    //spinlock_release(&stat.lock);
    return s;
}

uint32_t invalidation_stat(void){
    //spinlock_acquire(&stat.lock);
    uint32_t s = stat.tlb_invalidations;
    //spinlock_release(&stat.lock);
    return s;
}
uint32_t reloads_stat(void){
    //spinlock_acquire(&stat.lock);
    uint32_t s = stat.tlb_reloads;
    //spinlock_release(&stat.lock);
    return s;
}

uint32_t pt_fault_stats(int type){
    //spinlock_acquire(&stat.lock);
    uint32_t s=0;
    switch (type)
    {
    case ZEROED:
        s = stat.pt_zeroed_faults;
        break;
    case DISK:
        s = stat.pt_disk_faults;
        break;
    case ELF:
        s = stat.pt_elf_faults;
        break;
    case SWAPFILE:
        s = stat.pt_swapfile_faults;
        break;

    default:
        break;
    }
    //spinlock_release(&stat.lock);
    return s;
}
uint32_t swap_write_stat(void){
    uint32_t s;
    //spinlock_acquire(&stat.lock);
    s = stat.swap_writes;
    //spinlock_release(&stat.lock);
    return s;
}
/*UTILITY FUNCTIONS*/
void add_tlb_fault(void){
    //spinlock_acquire(&stat.lock);
    stat.tlb_faults++;
    //spinlock_release(&stat.lock);
}
void add_tlb_type_fault(int type){
    //spinlock_acquire(&stat.lock);
    //add_tlb_fault(); ??? where do I do that
    switch (type)
    {
    case FAULT_W_FREE:
        stat.tlb_free_faults++;
       
        break;
    case FAULT_W_REPLACE:
        stat.tlb_replace_faults++;
        break;
    default:
        break;
    }
    //spinlock_release(&stat.lock);
}
void add_tlb_invalidation(void){
    //spinlock_acquire(&stat.lock);
    stat.tlb_invalidations++;
    //spinlock_release(&stat.lock);
}

void add_tlb_reload(void){
    //spinlock_acquire(&stat.lock);
    stat.tlb_reloads++;
    //spinlock_release(&stat.lock);
}

void add_pt_type_fault(int type){
    //spinlock_acquire(&stat.lock);
    switch (type)
        {
        case ZEROED:
            stat.pt_zeroed_faults++;
            break;
        case DISK:
            stat.pt_disk_faults++;
            break;
        case ELF:
            stat.pt_elf_faults++;
            break;
        case SWAPFILE:
            stat.pt_swapfile_faults++;
            break;

        default:
            break;
        }
        //spinlock_release(&stat.lock);
 
}
void add_swap_writes(void){
    //spinlock_acquire(&stat.lock);
    stat.swap_writes++;
    //spinlock_release(&stat.lock);
}

void print_stats(void){
    uint32_t faults, free_faults, replace_faults, invalidations, reloads,
             pf_zeroed, pf_disk, pf_elf, pf_swap,
             swap_writes;
    //spinlock_acquire(&stat.lock);
    /*TLB stats*/
    faults = tlb_fault_stats();
    free_faults = tlb_type_fault_stats(FAULT_W_FREE);
    replace_faults = tlb_type_fault_stats(FAULT_W_REPLACE);
    invalidations = invalidation_stat();
    reloads = reloads_stat();
    /*PT stats*/
    pf_zeroed = pt_fault_stats(ZEROED);
    pf_disk = pt_fault_stats(DISK);
    pf_elf = pt_fault_stats(ELF);
    pf_swap = pt_fault_stats(SWAPFILE);
    /*swap writes*/
    swap_writes = swap_write_stat();
    //spinlock_release(&stat.lock);
    /*print statistics and errors if present*/
    kprintf("TLB stats: TLB faults = %d\tTLB Faults with Free = %d\tTLB Faults with Replace = %d\tTLB Invalidations = %d\tTLB Reloads = %d\n", 
            faults, free_faults, replace_faults, invalidations, reloads);
    kprintf("PT stats: Page Faults(Zeroed) = %d\tPage Faults(Disk) = %d\tPage Faults from Elf = %d\tPage Faults from Swapfile = %d\n", 
            pf_zeroed, pf_disk, pf_elf, pf_swap);
    kprintf("Swapfile writes = %d\n", swap_writes);
    if(faults!=(free_faults + replace_faults))
        kprintf("ERROR-constraint1: sum of TLB Faults with Free and TLB faults with replace should be equal to TLB Faults\n");
    if((reloads+pf_disk+pf_zeroed) != faults)
        kprintf("ERROR-constraint2: sum of TLB reloads, Page Faults(Disk) and Page Fault(Zeroed) should be equal to TLB Faults\n");
    if((pf_elf+pf_swap)!=pf_disk)
        kprintf("ERROR-constraint3: sum of Page Faults from ELF and Page Faults from Swapfile should be equal to Page Faults(Disk)\n");

}