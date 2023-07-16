#ifndef _VMSTATS_H_
#define _VMSTATS_H_
#include <types.h>
#include <lib.h>
#include <spinlock.h>

/*CONSTANTS*/

#define FAULT_W_FREE 0
#define FAULT_W_REPLACE 1

#define ZEROED 0
#define DISK 1
#define ELF 2
#define SWAPFILE 3
/**
 * Data structure with a field for each needed statistic.
*/
struct stats{
    uint32_t tlb_faults, tlb_free_faults, tlb_replace_faults, tlb_invalidations, tlb_reloads,
            pt_zeroed_faults, pt_disk_faults, pt_elf_faults, pt_swapfile_faults,
            swap_writes;
    struct spinlock lock; 
     /*It might be necessary to insert additional fields, for "temporary results"*/
}stat;

/*
 * This function is used to initialize stats
 */
void init_stat(void);

/*
 * This function returns the following statistics:
 * -TLB faults
 * 
 * @param: type of fault
 */
uint32_t tlb_fault_stats(void);

/*
 * This function is used to manage the following statistics:
 * -TLB faults with free
 * -TLB faults with replace
 * 
 * @param: type of fault
 */
uint32_t tlb_type_fault_stats(int);

/*
 * This function returns following statistics:
 * -TLB invalidations
 */
uint32_t invalidation_stat(void);

/*
 * This function returns the following statistics:
 * -TLB reloads
 */
uint32_t reloads_stat(void);

/*
 * This function returns the following statistics:
 * -Page fault (Zeroed)
 * -Page faults (Disk)
 * -Page faults (ELF)
 * -Page faults (Swapfile)
 * 
 * @param: type of fault
 */
uint32_t pt_fault_stats(int);

/*
 * This function returns the following statistics:
 * -Swapfile writes
 */
uint32_t swap_write_stat(void);

/* ------ UTILITY FUNCTIONS------- */


/**
 * This function increments the value of "tlb_faults"
*/
void add_tlb_fault(void);

/**
 * This function increments the correct field of the structure according to a "type" parameter passed as an argument.
 * type can be either: 
 * - FAULT_W_FREE (0)
 * - FAULT_W_REPLACE (1)
 * as defined in this header file
*/
void add_tlb_type_fault(int type);

/**
 * This function increments the value of the field "tlb_invalidations" each time the whole TLB is invalidated due to a process switch.
*/
void add_tlb_invalidation(void);

/**
 * This function increments the value tlb_reloads each time there is a TLB fault for a page that is already in memory.
*/
void add_tlb_reload(void);

/**
 * This function increments the value of the correct statistic on the page faults accordig to a type received as a parameter. This type can be either
 * - ZEROED (0)
 * - DISK (1)
 * - ELF (2)
 * - SWAPFILE (3)
 * as defined in this header file
*/
void add_pt_type_fault(int);

/**
 * This function increments the value of "swap_writes" each time there has been a page fault that required
 * writing a page to the swap file.
*/
void add_swap_writes(void);

/**
 * This function is called by vm_shutdown and prints the current statistics. In case of incorrect statistics, 
 * an error message is displayed. 
 * For the statistics to be correct, three costraints have to be respected.
 * (1)  the sum of "TLB Faults with Free" and "TLB Faults with Replace" should be equal to "TLB Faults"
 * (2)  the sum of "TLB Reloads", "Page Faults (Disk)"," and "Page Faults (Zeroed)"" should be equal to "TLB Faults"
 * (3) the sum of "Page Faults from ELF" and "Page Faults from Swapfile" should be equal to "Page Faults (Disk)""
*/
void print_stats(void);
#endif
