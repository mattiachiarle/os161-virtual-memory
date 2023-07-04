#include "vmstats.h"
void init_stat(void){
    spinlock_init(&stat.lock);
    stat.tlb_faults=0;
    stat.faults=0;
    stat.tlb_free_faults=0;
    stat.tlb_replace_faults = 0;
    stat.tlb_invalidations=0;
    stat.tlb_reloads=0;
    /*It might be necessary to insert additional initializations, for "temporary results"*/
}

uint32_t tlb_fault_stats(void){
    spinlock_acquire(&stat.lock);
    uint32_t s =  stat.tlb_faults;
    spinlock_release(&stat.lock);
    return s;
}
uint32_t tlb_type_fault_stats(int type){
    spinlock_acquire(&stat.lock);
    uint32_t s;
    switch (type)
    {
    case FAULT_W_FREE:
      s =  stat.tlb_free_faults;
      break;
    case FAULT_W_REPLACE:
        s = stat.tlb_replace_faults;
        break;
    default:
        break;
    }
    spinlock_release(&stat.lock);
    return s;
}

uint32_t invalidation_stat(void){
    spinlock_acquire(&stat.lock);
    uint32_t s = stat.tlb_invalidations;
    spinlock_release(&stat.lock);
    return s;
}
uint32_t reloads_stat(void){
    spinlock_acquire(&stat.lock);
    uint32_t s = stat.tlb_reloads;
    spinlock_release(&stat.lock);
    return s;
}
/*UTILITY FUNCTIONS*/
void add_tlb_fault(void){
    spinlock_acquire(&stat.lock);
    stat.tlb_faults++;
    spinlock_release(&stat.lock);
}
void add_tlb_type_fault(int type){
    spinlock_acquire(&stat.lock);
    switch (type)
    {
    case FAULT_W_FREE:
        stat.tlb_free_faults++;
        break;
    case FAULT_W_REPLACE:
        stat.tlb_replace_faults++;
        break;
    default:
        break;
    }
    spinlock_release(&stat.lock);
}
void add_tlb_invalidation(void){
    spinlock_acquire(&stat.lock);
    stat.tlb_invalidations++;
    spinlock_release(&stat.lock);
}

void add_tlb_reload(void){
    spinlock_acquire(&stat.lock);
    stat.tlb_reloads++;
    spinlock_release(&stat.lock);
}