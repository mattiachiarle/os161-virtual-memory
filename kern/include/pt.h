#ifndef _PT_H_
#define _PT_H_


#include "types.h"

/*
 * Data structure to handle the page table
 *
 */
struct pt_entry
{                 // this is an entry of our IPT
    vaddr_t page; // virt page in the frame
    pid_t pid;    // processID
    uint8_t ctl;  // some bits for control; from the lower:  Validity bit, Reference bit, isInTLB bit, ...
                 //  could be added other bits
    // add a lock here
} entr;

struct ptInfo
{
    struct pt_entry *pt; // our IPT
    int ptSize;          // IPT size, in number of pte
    paddr_t firstfreepaddr;
    struct lock *ptlock;
    
} peps;

/*
 * PT INIT
 */
void
pt_init(void);

/*
 * This function converts a logical address into a physical one.
 *
 * @param: virtual address that we want to access
 * @param: pid of the process that asks for the page
 *
 * @return: -1 if the requested page isn't stored in the page table,
 * physical address otherwise
 */

int pt_get_paddr(vaddr_t, pid_t);


/*  THIS IS THE BIG WRAPPER OF ALL OTHER FUNCS
 * find the cur PID and calls getpaddr that returns something.
 * IF pt_get_paddr is !== -1 return the paddr
 * ELSE calls function findspace() that finds a free space in IPT.
 *      IF found calls LOAD_PAGE Mattia's function
 *      ELSE calls find_victim that remove an entry and then LOAD_PAGE Mattia's function
 */
paddr_t get_page(vaddr_t);

/*
        wrapper for getpaddr
        it receives only vaddr and it takes pid from curproc
        if returns NULL call mattia's functions else return paddr
*/

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
// paddr_t load_page(vaddr_t, pid_t);

paddr_t find_victim(void);

/*
 * This function frees all the pages of a process after its termination.
 *
 * @param: pid of the ended process
 *
 * @return: -1 iin case of errors, 0 otherwise
 */
void free_pages(pid_t);


int cabodi(vaddr_t);

#endif