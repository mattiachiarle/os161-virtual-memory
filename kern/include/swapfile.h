#ifndef _SWAPFILE_H_
#define _SWAPFILE_H_

#include "types.h"
#include "addrspace.h"
#include "kern/fcntl.h"
#include "uio.h"
#include "vnode.h"
#include "copyinout.h"
#include "lib.h"
#include "vfs.h"

/*
 * Data structure to store the association 
 * (virtual address-pid) -> swapfile position
 */
struct swapfile{
    struct swap_cell *elements;
    struct vnode *v;
    size_t size;
};

struct swap_cell{
    pid_t pid; //it is used also as a flag to check if a certain cell is free or not
    vaddr_t vaddr;
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
int load_swap(vaddr_t, pid_t, paddr_t);

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
int store_swap(vaddr_t, pid_t, paddr_t);

int swap_init(void);

void remove_process_from_swap(pid_t pid);

#endif /* _SWAPFILE_H_ */
