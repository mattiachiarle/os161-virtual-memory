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

/*
 * Given the virtual address vaddr, it finds the corresponding page and it loads it into the provided paddr.
 */
int load_page(vaddr_t vaddr, pid_t pid, paddr_t paddr);

#endif