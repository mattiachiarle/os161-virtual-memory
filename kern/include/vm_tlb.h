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
 * TODO: I don't think that the cached bit is needed anymore
 * 
 * @return: -1 if any error occurred, otherwise the index of the removed entry
 */
int tlb_remove(void);

/*this is the functions that finsd the entry to sacrifice*/
int tlb_victim(void);

/*this function tells me whether the address is in the text segment (code segment) and therefore is not writable*/
int segment_is_readonly(vaddr_t vaddr);

/*this functions inserts the faultvaddr into the tlb with the corresponding phisical address*/
int tlb_insert(vaddr_t vaddr, paddr_t faultpaddr);

/**this entry tells me if the entry of the tlb is valid
*the validity bit is stored in the lower part
*/
int tlb_entry_is_valid(int i);

/**
 * this function inavlidates the entry corresponding to the given physical address.
 * It is useful when an entry in the PT is invalidated and the change has to be propagated to the TLB.
 * @return: 0 if ok
*/
int tlb_invalidate_entry(paddr_t paddr);