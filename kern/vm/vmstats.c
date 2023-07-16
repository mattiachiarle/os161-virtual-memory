#include "vmstats.h"

/*
 * This function is used to initialize stats
 */
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
    /*Other additional fields can be added if needed*/
}

/**
 * This function returns the statistics tlb_faults.
*/
uint32_t tlb_fault_stats(void){
    //spinlock_acquire(&stat.lock); // all the locks can be re-enabled if needed. We have commented them because they led to deadlocks and didn't bring significant contributions
    uint32_t s =  stat.tlb_faults;
    //spinlock_release(&stat.lock);
    return s;
}

/**
 * This function returns the desired statistic according to a type parameter that is passed as an argument.
 * The type can be either:
 * - FAULT_W_FREE (0)
 * - FAULT_W_REPLACE (1)
 * as defined in the header file
*/
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

/**
 * This function returns the statistic "tlb_invalidations", which tells us how many times the entire
 * TLB was invalidated.
*/
uint32_t invalidation_stat(void){
    //spinlock_acquire(&stat.lock);
    uint32_t s = stat.tlb_invalidations;
    //spinlock_release(&stat.lock);
    return s;
}

/**
 * This function returns the statistics tlb_reloads which tells us how many times there has been a
 * TLB fault for a page that is already in memory.
*/
uint32_t reloads_stat(void){
    //spinlock_acquire(&stat.lock);
    uint32_t s = stat.tlb_reloads;
    //spinlock_release(&stat.lock);
    return s;
}

/**
 * This function returns the correct statistic about the Page Table according to a type parameter 
 * passed as an argument. Type can be either:
 * - ZEROED (0)
 * - DISK (1)
 * - ELF (2)
 * - SWAPFILE (3)
 * as defined in the header file.
*/
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

/**
 * This function returns the statistic swap_writes, which tells us how many times there has been a 
 * page fault that required writing a page to the swap file.
*/
uint32_t swap_write_stat(void){
    uint32_t s;
    //spinlock_acquire(&stat.lock);
    s = stat.swap_writes;
    //spinlock_release(&stat.lock);
    return s;
}

/*-----------------------------UTILITY FUNCTIONS-----------------------------------------------------*/

/**
 * This function increments the value of "tlb_faults"
*/
void add_tlb_fault(void){
    //spinlock_acquire(&stat.lock);
    stat.tlb_faults++;
    //spinlock_release(&stat.lock);
}

/**
 * This function increments the correct field of the structure according to a "type" parameter passed as an argument.
 * type can be either: 
 * - FAULT_W_FREE (0)
 * - FAULT_W_REPLACE (1)
 * as defined in the header file
*/
void add_tlb_type_fault(int type){
    //spinlock_acquire(&stat.lock);

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

/**
 * This function increments the value of the field "tlb_invalidations" each time the whole TLB is invalidated due to a process switch.
*/
void add_tlb_invalidation(void){
    //spinlock_acquire(&stat.lock);
    stat.tlb_invalidations++;
    //spinlock_release(&stat.lock);
}

/**
 * This function increments the value tlb_reloads each time there is a TLB fault for a page that is already in memory.
*/
void add_tlb_reload(void){
    //spinlock_acquire(&stat.lock);
    stat.tlb_reloads++;
    //spinlock_release(&stat.lock);
}

/**
 * This function increments the value of the correct statistic on the page faults accordig to a type received as a parameter. This type can be either
 * - ZEROED (0)
 * - DISK (1)
 * - ELF (2)
 * - SWAPFILE (3)
 * as defined in the header file
*/
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

/**
 * This function increments the value of "swap_writes" each time there has been a page fault that required
 * writing a page to the swap file.
*/
void add_swap_writes(void){
    //spinlock_acquire(&stat.lock);
    stat.swap_writes++;
    //spinlock_release(&stat.lock);
}

/**
 * This function is called by vm_shutdown and prints the current statistics. In case of incorrect statistics, 
 * an error message is displayed. 
 * For the statistics to be correct, three costraints have to be respected.
 * (1)  the sum of "TLB Faults with Free" and "TLB Faults with Replace" should be equal to "TLB Faults"
 * (2)  the sum of "TLB Reloads", "Page Faults (Disk)"," and "Page Faults (Zeroed)"" should be equal to "TLB Faults"
 * (3) the sum of "Page Faults from ELF" and "Page Faults from Swapfile" should be equal to "Page Faults (Disk)""
*/
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
    /*check on constraint 1*/
    if(faults!=(free_faults + replace_faults))
        kprintf("ERROR-constraint1: sum of TLB Faults with Free and TLB faults with replace should be equal to TLB Faults\n");
    /*check on constraint 2*/
    if((reloads+pf_disk+pf_zeroed) != faults)
        kprintf("ERROR-constraint2: sum of TLB reloads, Page Faults(Disk) and Page Fault(Zeroed) should be equal to TLB Faults\n");
    /*check on constraint 3*/
    if((pf_elf+pf_swap)!=pf_disk)
        kprintf("ERROR-constraint3: sum of Page Faults from ELF and Page Faults from Swapfile should be equal to Page Faults(Disk)\n");

}