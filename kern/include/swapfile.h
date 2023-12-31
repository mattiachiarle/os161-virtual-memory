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
#include "vmstats.h"
#include "synch.h"
#include "proc.h"
#include "opt-sw_list.h"
#include "vm.h"
#include "opt-debug.h"
#include "spl.h"
#include "current.h"

/**
 * Data structure to store the association 
 * (virtual address-pid) -> swapfile position
 */
struct swapfile{
    #if OPT_SW_LIST
    struct swap_cell **text;//Array of lists of text pages in the swapfile (one for each pid)
    struct swap_cell **data;//Array of lists of data pages in the swapfile (one for each pid)
    struct swap_cell **stack;//Array of lists of stack pages in the swapfile (one for each pid)
    struct swap_cell *free;//List of free pages in the swapfile
    void *kbuf;//Buffer used during the copy of swap pages
    #else
    struct swap_cell *elements;//Array of lists in the swapfile (one for each pid)
    #endif
    struct vnode *v;//vnode of the swapfile
    int size;//Number of pages stored in the swapfile
};

/**
 * Information related to a single page of the swapfile
*/
struct swap_cell{
    vaddr_t vaddr;//Virtual address corresponding to the stored page
    int store;//Flag that tells to us if we're performing a store operation on a specific page or not
    #if OPT_SW_LIST
    struct swap_cell *next;
    paddr_t offset;//Offset of the swap element within the swapfile
    struct cv *cell_cv;//Used to wait for the store operation to end
    struct lock *cell_lock;//Necessary to perform cv_wait
    #else
    pid_t pid; //Pid of the process that owns that page. If pid=-1 the page is free
    #endif
};

/**
 * This function puts back a frame in RAM.
 *
 * @param vaddr_t: virtual address that caused the page fault
 * @param pid_t: pid of the process
 * @param paddr_t: physical address of the RAM frame to use
 * 
 * @return 1 if the page was found in the swapfile, 0 otherwise
*/
int load_swap(vaddr_t, pid_t, paddr_t);

/**
 * This function saves a frame into the swapfile.
 * If the swapfile has size>9MB, it raises kernel panic.
 *
 * @param vaddr_t: virtual address that caused the page fault
 * @param pid_t: pid of the process
 * @param paddr_t: physical address of the RAM frame to save
 * 
 * @return -1 in case of errors, 0 otherwise
*/
int store_swap(vaddr_t, pid_t, paddr_t);

/**
 * This function initializes the swap file. In particular, it allocates the needed data structures and it opens the file that will store the pages.
*/
int swap_init(void);

/**
 * When a process terminates, we mark as free all its pages stored into the swapfile.
 * 
 * @param pid_t: pid of the ended process.
*/
void remove_process_from_swap(pid_t);

/**
 * When a fork is executed, we copy all the pages of the old process for the new process too.
 * 
 * @param pid_t: pid of the old process.
 * @param pid_t: pid of the new process.
*/
void copy_swap_pages(pid_t, pid_t);

/**
 * Debugging function. Given the pid, it prints text, data and stack lists.
 * 
 * @param pid_t: pid of the process.
*/
void print_list(pid_t);

/**
 * Optimization function. After the end of the whole program, we sort again all the pages in the swapfile.
 * In fact, the smaller the offset the faster the I/O, and so thanks to this function we don't worsen performances.
*/
void reorder_swapfile(void);

#endif /* _SWAPFILE_H_ */
