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