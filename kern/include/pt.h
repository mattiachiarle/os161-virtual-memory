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
int nkmalloc; // nkmalloc = number of pages allocated with kmalloc - number of pages freed
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
    struct pt_entry *pt;    // our IPT
    int ptSize;             // IPT size, in number of pte
    paddr_t firstfreepaddr; // Offset to use to compute the physical address of the frames
    struct lock *pt_lock;   // Necessary for the cv
    struct cv *pt_cv;       // Used to sleep if the IPT is full
    int *contiguous;        // Used to keep track of how many pages we need to free
} peps;

struct hashentry // single entry of hashtable
{
    int iptentry;           // "ptr" to IPT entry
    vaddr_t vad;            // virtual address of the entry
    pid_t pid;              // pid of the entry
    struct hashentry *next; // ptr to next hashentry
};

struct hashT // struct
{
    struct hashentry **table; // array of list of hashentry with dimension size.
    int size;                 // 2 times the IPT
} htable;

struct hashentry *unusedptrlist; // list where all unused blocks for the hast table are stored

/**
 * It initializes the page table.
 */
void pt_init(void);

/**
 * This function gets the physical address from the IPT, then updated with the Hash table
 *
 * @param vaddr_t: virtual address
 * @param pid_t: pid of the process
 *
 *
 * @return -1 if the requested page isn't stored in the page table, physical address otherwise
 */
int pt_get_paddr(vaddr_t, pid_t);

/**
 * This function is a wrapper for the following process:
 *  if physical frame found return directly the current physical address
 *  if not found find a position to insert the new element
 *  if all full find a victim
 *  return the physical position of the page
 *
 * @param vaddr_t: virtual address
 *
 *
 * @return physical address found inside the IPT
 */
paddr_t get_page(vaddr_t);

/**
 * This function finds a victim in the IPT and the updates the hash table.
 *  it uses a second chance algorithm based on TLB presence and reference bit
 * @param vaddr_t: virtual address
 * @param pid_t: pid of the process
 *
 *
 * @return physical address found inside the IPT
 */
int find_victim(vaddr_t, pid_t);

/**
 * This function frees all the pages inside the IPT
 *
 * @param vaddr_t: virtual address
 * @param pid_t: pid of the process
 *
 * @return void
 */
void free_pages(pid_t);

/**
 * This function inserts into the hash table, from unusedptrlist, a page, in order to fastly find the index
 *
 * @param vaddr_t: virtual address
 * @param pid_t: pid of the process
 * @param int: page index inside the IPT
 *
 * @return void
 */
void add_in_hash(vaddr_t, pid_t, int);

/**
 * This function advices that a frame(virtual address) is removed from TLB.
 *
 * @param vaddr_t: virtual address
 *
 *
 * @return 1 if everything ok, -1 otherwise
 */
int update_tlb_bit(vaddr_t, pid_t);

/**
 * This function inserts in the IPT some kernel memory in a contiguous way
 *
 * @param int: the number of pages to allocate
 *
 * @return physical address found inside the IPT
 */
paddr_t get_contiguous_pages(int);

/**
 * This function retrieves the correct intex of the IPT from the hash table
 *
 * @param vaddr_t: virtual address
 * @param pid_t: pid of the process
 *
 *
 * @return the IPT entry value, -1 otherwise
 */
int get_index_from_hash(vaddr_t, pid_t);

/**
 * This function frees the contiguous pages allocated in IPT
 *
 * @param vaddr_t: virtual address
 *
 *
 */
void free_contiguous_pages(vaddr_t);

/**
 * This function is used to copy inside PT or Swap-file all the pages of the old pid for the new one
 *
 * @param pid_t: old pid to copy from
 * @param pid_t: new pid to add for each page
 *
 */
void copy_pt_entries(pid_t, pid_t);

/**
 * This function setups some bits before the copy_pt_entries related to a fork
 *
 * @param pid_t: pages with that pid will be prepared for SWAP
 *
 *
 * @return void
 */
void prepare_copy_pt(pid_t);

/**
 * This function setups setups to zero all the SWAP bits related to the passed pid
 *
 * @param pid_t: pages with this pid will be updated with SWAP-bit=0
 *
 *
 * @return void
 */
void end_copy_pt(pid_t);

/**
 * Debugging function, used to print number of kmalloc - number of kfree
*/
void print_nkmalloc(void);

/**
 * This function removes from the hash table a list block and adds it again in unusedptrlist
 *
 *
 * @param vaddr_t: virtual address
 * @param pid_t: pid of the process
 *
 *
 * @return physical address found inside the IPT
 */
void remove_from_hash(vaddr_t, pid_t);

/**
 * This function uses an hash function in order to calculate the entry in the hash table
 *
 * @param vaddr_t: virtual address
 * @param pid_t: pid of the process
 *
 *
 * @return the entry in the hash table
 */
int get_hash_func(vaddr_t, pid_t);

/**
 * This function initializes the hash table.
 */
void htable_init(void);

#endif