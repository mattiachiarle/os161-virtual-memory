#include "vm_tlb.h"
/*
 * vm.h includes the definition of vm_fault, which is used to handle the
 * TLB misses
 */
#include "vm.h"
#include "vmstats.h"

int tlb_remove(void){
    return -1;
}

int vm_fault(int faulttype, vaddr_t faultaddress){
    return -1;
}