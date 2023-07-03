/*
 * Data structure to manage the algorithm used in tlb_remove.
 *
 */
struct tlb{

};

/*
 * This function is called after a TLB miss, and it's used to remove
 * an entry from the TLB to store the new one.
 *
 * The first version will use a round robin replacement algorithm,
 * while the second one will use a FIFO strategy.
 * 
 * It'll call a function of the page table to update some relevant
 * fields (like the cached bit).
 * 
 * @return: -1 if any error occurred, otherwise the index of the removed entry
 */
int tlb_remove(void);