#ifndef _VM_TLB_H_
#define _VM_TLB_H_

#include "types.h"
#include "syscall.h"
#include "proc.h"
#include "opt-debug.h"

pid_t previous_pid;

/*
 * Data structure to manage the algorithm used in tlb_remove.
 *
 */
struct tlb{

};

pid_t old_pid;

/*not needed anymore, we leave it here in case we want it to be a wrapper for the mips instruction TLB_INVALIDATE*/
int tlb_remove(void);

/**
 * This function chooses the entry to sacrifice in the TLB and returns its index.
*/
int tlb_victim(void);

/*this function tells me whether the address is in the text segment (code segment) and therefore is not writable*/
int segment_is_readonly(vaddr_t vaddr);

/**
 * This function writes a new entry in the TLB. It receives as parameters the fault address (virtual) and the 
 * corresponding physical one received by the page table. 
 * It finds a space to insert the entry (vaddr, paddr) by means of the tlb_victim.
*/
int tlb_insert(vaddr_t vaddr, paddr_t faultpaddr);

/**
 * This function tells me if the entry in the TLB at index i is valid or not.
*/
int tlb_entry_is_valid(int i);

/**
 * DISCLAIMER: this function has never been used.
 * this function inavlidates the entry corresponding to the given physical address.
 * It is useful when an entry in the PT is invalidated and the change has to be propagated to the TLB.
 * @return: 0 if ok
*/
int tlb_invalidate_entry(paddr_t paddr);

/**
 * this function has to invalidate the TLB when there is a switch from a process to another. Indeed, 
 * the TLB is common for all processes and does not have a "pid" field.
*/
void tlb_invalidate_all(void);

/**
 * Useful for debugging reasons eheh :^)
*/
void print_tlb(void);


#endif