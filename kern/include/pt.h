/*
 * Data structure to handle the page table
 */
struct pt{

};

/*
 * This function converts a logical address into a physical one.
 *
 * @param: virtual address that we want to access
 * @param: pid of the process that asks for the page
 * 
 * @return: NULL if the requested page isn't stored in the page table, 
 * physical address otherwise
 */
paddr_t pt_get_paddr(vaddr_t,pid_t);

/*
 * This function loads a new page from the swapfile or the elf file
 * (depending on the virtual address provided). If the page table is full,
 * it selects the page to remove by using second-chance and it stores it in
 * the swapfile.
 *
 * @param: virtual address that we want to access
 * @param: pid of the process that asks for the page
 * 
 * @return: NULL in case of errors, physical address otherwise
 */
paddr_t load_page(vaddr_t,pid_t);

/*
 * This function frees all the pages of a process after its termination.
 *
 * @param: pid of the ended process
 * 
 * @return: -1 iin case of errors, 0 otherwise
 */
int free_pages(pid_t);