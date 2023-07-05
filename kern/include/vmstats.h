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
/*
 * Data structure to store the statistics
*/
struct stats{
    uint32_t tlb_faults, tlb_free_faults, tlb_replace_faults, tlb_invalidations, tlb_reloads,
            pt_zeroed_faults, pt_disk_faults, pt_elf_faults, pt_swapfile_faults,
            swap_writes;
    struct spinlock lock; 
     /*It might be necessary to insert additional fields, for "temporary results"*/
}stat;

/*
 * This function is used to initializa stats
 */
void init_stat(void);

/*
 * This function is used to manage the following statistics:
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
 * This function is used to manage the following statistics:
 * -TLB invalidations
 */
uint32_t invalidation_stat(void);

/*
 * This function is used to manage the following statistics:
 * -TLB reloads
 */
uint32_t reloads_stat(void);

/*
 * This function is used to manage the following statistics:
 * -Page fault (Zeroed)
 * -Page faults (Disk)
 * -Page faults (ELF)
 * -Page faults (Swapfile)
 * 
 * @param: type of fault
 */
uint32_t pt_fault_stats(int);

/*
 * This function is used to manage the following statistics:
 * -Swapfile writes
 */
uint32_t swap_write_stat(void);

/*UTILITY FUNCTIONS*/
void add_tlb_fault(void);
void add_tlb_type_fault(int type);
void add_tlb_invalidation(void);
void add_tlb_reload(void);
void add_pt_type_fault(int);
void add_swap_writes(void);
/*this function is called by vm_shutdown to display stats*/
void print_stats(void);
#endif
