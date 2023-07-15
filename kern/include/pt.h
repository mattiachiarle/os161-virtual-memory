#ifndef _PT_H_
#define _PT_H_

#include "types.h"
#include "addrspace.h"
#include "kern/errno.h"
#include "synch.h"
#include "spl.h"
#include "opt-debug.h"

int pt_active;
#if OPT_DEBUG
int nkmalloc; //nkmalloc = number of pages allocated with kmalloc - number of pages freed
#endif

/*
 * Data structure to handle the page table
 *
 */
struct pt_entry
{                 // this is an entry of our IPT
    vaddr_t page; // virt page in the frame
    pid_t pid;    // processID
    uint8_t ctl;  // some bits for control; from the lower:  Validity bit, Reference bit, isInTLB bit, ...
} entr;

struct ptInfo
{
    struct pt_entry *pt; // our IPT
    int ptSize;          // IPT size, in number of pte
    paddr_t firstfreepaddr; //Offset to use to compute the physical address of the frames
    struct lock *pt_lock; //Necessary for the cv
    struct cv *pt_cv; //Used to sleep if the IPT is full
    int *contiguous; //Used to keep track of how many pages we need to free
} peps;

struct hashentry  // single entry of hashtable
{
    int iptentry;   // "ptr" to IPT entry
    vaddr_t vad; //virtual address of the entry
    pid_t pid; //pid of the entry
    struct hashentry *next;  //ptr to next hashentry
};

struct hashT   // struct   
{
    struct hashentry **table;   // array of list of hashentry with dimension size.
    int size; // 2 times the IPT
} htable;

struct hashentry *unusedptrlist;  // list where all unused blocks for the hast table are stored

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

int find_victim(vaddr_t, pid_t);

/*
 * This function frees all the pages of a process after its termination.
 *
 * @param: pid of the ended process
 *
 * @return: -1 iin case of errors, 0 otherwise
 */
void free_pages(pid_t);

void add_in_hash(vaddr_t, pid_t, int);

int update_tlb_bit(vaddr_t, pid_t);

paddr_t get_contiguous_pages(int);

int get_index_from_hash(vaddr_t, pid_t);

void free_contiguous_pages(vaddr_t);

void copy_pt_entries(pid_t, pid_t);

void prepare_copy_pt(pid_t);

void end_copy_pt(pid_t);

void print_nkmalloc(void);

void remove_from_hash(vaddr_t, pid_t);

int get_hash_func(vaddr_t, pid_t);

void htable_init(void);

#endif