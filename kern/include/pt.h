#include "types.h"

/*
 * Data structure to handle the page table
 * 
 */
struct pt_entry{  //this is an entry of our IPT
    vaddr_t page;  //virt page in the frame
    pid_t pid;    // processID 
    uint8_t ctl;  //some bits for control; from the lower:  Validity bit, Reference bit, isInTLB bit, ...
                    // could be added other bits
}entr;

struct pt_entry *pt;  //our IPT
int ptSize;   //IPT size, in number of pte

/*
* PT INIT
*/
void pt_init(void);

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