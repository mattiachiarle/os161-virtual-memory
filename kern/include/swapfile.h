/*
 * Data structure to store the association 
 * (virtual address-pid) -> swapfile position
 */
struct swapfile{

};

/*
 * This function puts back a frame in RAM.
 *
 * @param: virtual address that caused the page fault
 * @param: pid of the process
 * @param: physical address of the RAM frame to use
 * 
 * @return: -1 in case of errors, 0 otherwise
*/
int load_page(vaddr_t, pid_t, paddr_t);

/*
 * This function saves a frame into the swapfile.
 * If the swapfile has size>9MB, it raises kernel panic.
 *
 * @param: virtual address that caused the page fault
 * @param: pid of the process
 * @param: physical address of the RAM frame to save
 * 
 * @return: -1 in case of errors, 0 otherwise
*/
int store_page(vaddr_t, pid_t, paddr_t);
