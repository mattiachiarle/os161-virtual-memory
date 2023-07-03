/*
 * Data structure to store the statistics
*/
struct stats{
    int faults, free_faults, replace_faults, invalidations, reloads;
};

/*
 * This function is used to initializa stats
 */
void init_stat(void);

/*
 * This function is used to manage the following statistics:
 * -TLB faults
 * -TLB faults with free
 * -TLB faults with replace
 * 
 * @param: type of fault
 */
void tlb_fault_stats(int);

/*
 * This function is used to manage the following statistics:
 * -TLB invalidations
 */
void invalidation_stat(void);

/*
 * This function is used to manage the following statistics:
 * -TLB reloads
 */
void reloads_stat(void);

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