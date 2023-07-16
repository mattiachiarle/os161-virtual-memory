#ifndef _SEGMENTS_H_
#define _SEGMENTS_H_

#include "swapfile.h"
#include "vm.h"
#include "proc.h"
#include "types.h"
#include "addrspace.h"
#include "elf.h"
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <elf.h>
#include "vmstats.h"
#include "opt-project.h"
#include "opt-debug.h"

/**
 * Given the virtual address vaddr, it finds the corresponding page and it loads it into the provided paddr.
 * 
 * @param vaddr: the virtual address that caused the page fault
 * @param pid: pid of the process that caused the page fault
 * @param paddr: the physical address in which we'll load the page
 * 
 * @return 0 if everything goes fine, otherwise the error code returned from VOP_READ
 */
int load_page(vaddr_t vaddr, pid_t pid, paddr_t paddr);

#endif