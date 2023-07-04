#include <types.h>
#include <lib.h>
#include <spinlock.h>

/*CONSTANTS*/

#define FAULT_W_FREE 0
#define FAULT_W_REPLACE 1
/*
 * Data structure to store the statistics
*/
struct stats{
    uint32_t tlb_faults, faults, tlb_free_faults, tlb_replace_faults, tlb_invalidations, tlb_reloads;
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
void pt_fault_stats(int);

/*
 * This function is used to manage the following statistics:
 * -Swapfile writes
 */
void swap_write_stat(void);

/*UTILITY FUNCTIONS*/
void add_tlb_fault(void);
void add_tlb_type_fault(int type);
void add_tlb_invalidation(void);
void add_tlb_reload(void);