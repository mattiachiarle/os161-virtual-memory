# os161-virtual-memory

This project, realized by [Francesca Fusco](https://github.com/FrancescaFusco00), [Giuseppe Gabriele](https://github.com/giuseppegabriele) and [Mattia Chiarle](https://github.com/mattiachiarle), will take care of implementing virtual memory with on demand paging on OS161 and error handling for readonly segments and segmentation fault.

To achieve it, we created an Inverted Page Table (IPT) and we modified the original implementation of the TLB. Furthermore, we introduced a swapfile to support swapping.

Our version of OS161 correctly runs all the most important test cases, that are (in order of complexity)

- palin
- matmult, sort, huge
- forktest
- parallelvm
- bigfork

All the previous tests can be found in testbin. Before running them, it is suggested to increase the RAM memory available to 2 MB (in `root/sys161.conf`) due to the additional data structures that we had to use. They can run also with 1 MB of RAM, although they are very slow due to the high number of swap performed.

For the swapfile, we used the raw partition of LHD0.img. In our implementation, we assumed that its size is 9 MB. Since by default the size of this partition is 5 MB, please increase it by running the following command inside root folder

```bash
disk161 resize LHD0.img 9M
```

We also slightly modified the implementation of the needed system calls (fork, waitpid, _exit), as well as some parts of `loadelf.c`. To have additional details about the single functions, you can keep reading the readme or directly check the comments in the .c and .h files, that describe in a precise way all the single instructions.

# SYNCHRONIZATION

Synchronization is not a trivial problem in the VM management. In fact, we have hidden synchronization techniques behind I/O operations that may cause deadlocks or errors even if our code is correct. In particular, since I/O synchronization is achieved by means of semaphores, that in our solution are implemented with cv and locks, our process can’t own any lock before starting an I/O operation. Furthermore, since a CPU can own only one spinlock at a time, it’s not possible that 2 processes (not necessarily the same one!) own 2 different spinlocks at the same time. Due to all these constraints, it’s immediately clear that a standard synchronization would have been really difficult to implement, and it would have been extremely hard to debug or understand where the issues where.

All these consideration led us to realize that the safest solution (although probably it’s neither the cleanest nor the most efficient one) was to disable the interrupts during all the critical operations. In fact, this allowed us to be sure that we wouldn’t be switched out during our execution, but without the potential conflicts with hidden synchronization techniques.

The only moments in which a process can be switched out are during an I/O operation or when we explicitly enforced it with cv_wait. You can check in `pt.c` and `swapfile.c` where and why we used them, but the general idea is that whenever a process can’t proceed on its own but needs to wait for something to happen (an entry that can become a victim, an I/O to finish, etc.) we enforce a thread switch with cv_wait. When the waiting condition may be satisfied, we notify the sleeping process. 

There are few exceptions to what we previously wrote, contained in particular in `pt_init`/`kmalloc`/`kfree` and in `sys_fork`.

For the first exception, the main problem is that, in our VM implementations, kmalloc is handled by the page table. However, to correctly initialize the page table we perform some kmalloc operations. To solve this problem we need to understand when we must rely on the page table and when on `getppages`, and the solution is to provide an exclusive access to `pt->active` by means os a spinlock.  Probably our VM would work even without it, but if the initialization phase becomes parallelized they avoid any problem.

For what concerns `sys_fork`, we decided to perform an optimization. As you can see in pt.c, during a fork operation we lock in the page table all the pages belonging to the old process. Although it’s necessary to have consistency during all the I/O operations, it is clear that if many processes perform a fork at the same time we risk to block the page table (i.e. we may risk a situation in which no page can be selected as a victim). To mitigate this downside we created a semaphore, so that only one process at a time can perform a fork.

# Statistics

## Statistics and their meaning

The following statistics have to be collected (we also report the names of the variables that in our code represent the statistics in exam):

1. **TLB Faults -** (`tlb_faults`)
    - Total amount of TLB misses (excluding the ones that caused the program to crash).
2. **TLB Faults with Free -** (`tlb_free_faults`)
    - The number of TLB misses that led to an insertion in an “empty” (*******invalid*******) space in the TLB. In other words, there was a free space in the TLB to add the new entry and no replacement was required.
3. **TLB Faults with Replace  -** (`tlb_replace_faults`)
    - The number of TLB misses that caused the choice of a victim to overwrite with the new entry.
4. **TLB Invalidations -**  (`tlb_invalidations`)
    - The number of times in which the entire TLB was invalidated. The operation is done every time there is a switch of process.
5. **TLB Reloads** (`tlb_reloads`)
    - The number of TLB misses caused by pages that were already in memory.
6. **Page Faults (Zeroed)** - (`pt_zeroed_faults`)
    - The number of TLB misses that required a new page to be zero-filled.
7. **Page Faults (Disk)**  - (`pt_disk_faults`)
    - The number of TLB misses that required the loading of a page from disk.
8. **Page Faults From Elf** - (`pt_elf_faults`)
    - The number of Page Faults that required getting a page from the ELF file
9. **Page Faults from Swapfile**  - (`pt_swapfile_faults`)
    - The number of page faults that required getting a page from the swap file.
10. **Swapfile Writes**  - (`swap_writes`)
    - The number of page faults that required writing a page to the swap file.

## Constraints

Some constraints have to be respected for the statistics to be correct.

- **************************Constraint 1**************************
    
    <aside>
    ⚠️ The sum of “*TLB faults with Free*” and “*TLB Faults with Replace*” should be equal “*TLB Faults*”.
    
    </aside>
    
- ************************Constraint 2************************
    
    <aside>
    ⚠️ The sum of “*TLB Reloads*”, “*Page Faults (Disk)*” and “*Page Faults (Zeroed)*” should be equal to “*TLB Faults*”.
    
    </aside>
    
    This means that the faults that result in the program to be killed should not be counted.
    
- **************************Constraint 3**************************
    
    <aside>
    ⚠️ The sum of “*Page Faults from ELF*” and “*Page Faults from Swapfile*” should be equal to ”*Page Faults (Disk)*”.
    
    </aside>
    
    In case one (or more) of the abovementioned conditions is not satisfied, an error message is displayed during the shutdown of the kernel (more explained later in the report).
    

## Implementation

The code described here is in 

```bash
kern/include/vmstats.h

kern/vm/vmstats.c
```

In the header, some constants have been defined to distinguish between the different kinds of statistics regarding the page table and the TLB.

```c
// for the TLB
#define FAULT_W_FREE 0
#define FAULT_W_REPLACE 1

// for the PT
#define ZEROED 0
#define DISK 1
#define ELF 2
#define SWAPFILE 3
```

For the management of the statistics, a data structure was also defined. 

The meaning of each field in the structure can be easily guessed by their names.

 

```c
struct stats{
    uint32_t tlb_faults, tlb_free_faults, tlb_replace_faults, tlb_invalidations, tlb_reloads,
            pt_zeroed_faults, pt_disk_faults, pt_elf_faults, pt_swapfile_faults,
            swap_writes;
    struct spinlock lock; 
     /*It might be necessary to insert additional fields, for "temporary results"*/
}stat;
```

<aside>
❗ The spinlock field, introduced to protect the structure from concurrent accesses, is never used in our final solution. This is due to the fact that its usage in the optimized version of the project led to a deadlock:  . 
Considering that, in our implementation, the accesses to the fields in exam are always sequential, we have decided to solve this problem by commenting the lines of code that made use of the spinlock.

</aside>

The most important functions implemented in the file are:

- `uint32_t tlb_fault_stats(void)`: this function returns the value of the statistics “tlb_faults”
- `uint32_t tlb_type_fault_stats(int type)`: this function returns the value of the desired statistics according to a type parameter passed as an argument. The value of *type* can be either FAULT_W_FREE or FAULT_W_REPLACE. In the first case, *tlb_free_faults* will be returned, in the second case the function will return the value of *tlb_replace_faults*.
    
    ```c
    uint32_t tlb_type_fault_stats(int type){
        uint32_t s=0;
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
        return s;
    }
    ```
    
- `uint32_t invalidation_stat(void)`: this function returns the value of the field *****************tlb_invalidations***************** of the data structure stat.
- `uint32_t reloads_stat(void)`: this function returns the statistics ***********tlb_reloads***********.
- `uint32_t pt_fault_stats(int type)`: this function returns the correct statistics about the Page Table according to a ****type**** parameter. The value of type can be either ZEROED, DISK, ELF, SWAPFILE, which return respectively *pt_zeroed_faults*, *pt_disk_faults*, *pt_elf_faults*, *pt_swapfile_faults*.
    
    ```c
    */
    uint32_t pt_fault_stats(int type){
        uint32_t s=0;
        switch (type)
        {
        case ZEROED:
            s = stat.pt_zeroed_faults;
            break;
        case DISK:
            s = stat.pt_disk_faults;
            break;
        case ELF:
            s = stat.pt_elf_faults;
            break;
        case SWAPFILE:
            s = stat.pt_swapfile_faults;
            break;
    
        default:
            break;
        }
        return s;
    }
    ```
    
- `uint32_t swap_write_stat(void)`: this function returns the value of the *swap_writes* statistics.

Utility functions

- `void add_tlb_fault(void)`: this function increments the value of *tlb_faults*
- `void add_tlb_type_fault(int type)`: this function increments the correct TLB statistics according to a "type" parameter passed as an argument, which can be either FAULT_W_FREE or FAULT_W_REPLACE
- `void add_tlb_invalidation(void)`: this function increments the value of the field *tlb_invalidations* each time it is called.
- `void add_tlb_reload(void)`: this function increments the value of the field *tlb_reloads* each time it is called.
- `void add_pt_type_fault(int)`: this function increments the value of the correct statistic on the page table according to a type received as a parameter. *type* can be either equal to ZEROED, DISK, ELF or SWAPFILE.

```c
void add_pt_type_fault(int type){
    switch (type)
        {
        case ZEROED:
            stat.pt_zeroed_faults++;
            break;
        case DISK:
            stat.pt_disk_faults++;
            break;
        case ELF:
            stat.pt_elf_faults++;
            break;
        case SWAPFILE:
            stat.pt_swapfile_faults++;
            break;

        default:
            break;
        }
 
}
```

- `void add_swap_writes(void)`: this function increments the value of *swap_writes*.
- `void print_stats(void)`: this function is called by vm_shutdown and prints the current statistics. In case of incorrect statistics, an error message is displayed.
    
    ```c
    void print_stats(void){
        uint32_t faults, free_faults, replace_faults, invalidations, reloads,
                 pf_zeroed, pf_disk, pf_elf, pf_swap,
                 swap_writes;
        //spinlock_acquire(&stat.lock);
        /*TLB stats*/
        faults = tlb_fault_stats();
        free_faults = tlb_type_fault_stats(FAULT_W_FREE);
        replace_faults = tlb_type_fault_stats(FAULT_W_REPLACE);
        invalidations = invalidation_stat();
        reloads = reloads_stat();
        /*PT stats*/
        pf_zeroed = pt_fault_stats(ZEROED);
        pf_disk = pt_fault_stats(DISK);
        pf_elf = pt_fault_stats(ELF);
        pf_swap = pt_fault_stats(SWAPFILE);
        /*swap writes*/
        swap_writes = swap_write_stat();
        //spinlock_release(&stat.lock);
        /*print statistics and errors if present*/
        kprintf("TLB stats: TLB faults = %d\tTLB Faults with Free = %d\tTLB Faults with Replace = %d\tTLB Invalidations = %d\tTLB Reloads = %d\n", 
                faults, free_faults, replace_faults, invalidations, reloads);
        kprintf("PT stats: Page Faults(Zeroed) = %d\tPage Faults(Disk) = %d\tPage Faults from Elf = %d\tPage Faults from Swapfile = %d\n", 
                pf_zeroed, pf_disk, pf_elf, pf_swap);
        kprintf("Swapfile writes = %d\n", swap_writes);
        /*check on constraint 1*/
        if(faults!=(free_faults + replace_faults))
            kprintf("ERROR-constraint1: sum of TLB Faults with Free and TLB faults with replace should be equal to TLB Faults\n");
        /*check on constraint 2*/
        if((reloads+pf_disk+pf_zeroed) != faults)
            kprintf("ERROR-constraint2: sum of TLB reloads, Page Faults(Disk) and Page Fault(Zeroed) should be equal to TLB Faults\n");
        /*check on constraint 3*/
        if((pf_elf+pf_swap)!=pf_disk)
            kprintf("ERROR-constraint3: sum of Page Faults from ELF and Page Faults from Swapfile should be equal to Page Faults(Disk)\n");
    
    }
    ```
    

# vm_faults and TLB management

The code related to this part can be found in the following files:

```bash
kern/include/vm.h

kern/include/vm_tlb.h

kern/vm/vm_tlb.c
```

### vm_fault

Each time there is a TLB miss, a trap code calls `vm_fault`. *****vm_fault*****, defined in ********vm_tlb.c********, checks if the fault was caused by an attempt to write in a readonly area. In that case, the process is ended; in all the other cases, the Page Table is asked to provide the physical address of the virtual address that caused the fault ( `get_page` is called). 

Finally, `tlb_insert` is used to insert the new entry into the TLB.

```jsx
int vm_fault(int faulttype, vaddr_t faultaddress){

    #if OPT_DEBUG
    print_tlb();
    #endif

    DEBUG(DB_VM,"\nfault address: 0x%x\n",faultaddress);
    int spl = splhigh(); // so that the control does not pass to another waiting process.
    paddr_t paddr;
  
    faultaddress &= PAGE_FRAME; // I extract the address of the frame that caused the fault (it was not in the TLB)

    /*I update the statistics*/
    add_tlb_fault();
    /*I extract the virtual address of the corresponding page*/
    switch (faulttype)
    {
    case VM_FAULT_READ:
        
        break;
    case VM_FAULT_WRITE:
      
        break;
        /*The readonly case hase to be cosidered special: the text segment cannot be written by the process. 
        Therefore, if the process tries to modify a RO segment, the process has to be ended by means of the 
        appropriate system call (no need to panic)*/
    case VM_FAULT_READONLY:
        kprintf("You tried to write a readonly segment... The process is ending...");
        sys__exit(0);
        break;
    
    default:
        break;
    }
    /*If I am here is either a VM_FAULT_READ or a VM_FAULT_WRITE*/
    /*was the address space set up correctly?*/
    KASSERT(as_is_ok() == 1);
   /*If the address space was set up correctly, I ask the Page table for the virtual address address of the frame that is not present in the TLB*/
    paddr = get_page(faultaddress);
    /*Now that I have the address, I can insert it into the TLB */
    tlb_insert(faultaddress, paddr);
    splx(spl);
    return 0;
}
```

### tlb_insert and read-only segments

This function writes a new entry in the TLB. It receives as parameters the fault address and the corresponding physical one received by the page table.

At first, it iterates on all of the entries of the TLB in order to find an invalid one (an invalid entry is indeed an “empty” entry). If it finds a free space, the new entry is inserted in the table.

If the TLB is full, the function `tlb_victim` chooses the entry to sacrifice (by means of a Round Robin algorithm) and returns its index. The entry is then overwritten with the new value.

Before inserting the new entry, however, some operations have to be performed. 

1. in case a replacement is being performed, the Page Table has to be informed that the previous content is not cached anymore. This is done by means of the function `update_tlb_bit`.
2. The validity bit has to be set in the content (the mask TLBLO_VALID is used)
3. In case the entry is not readonly, the Dirty bit has to be set. The Dirty bit is basically a write privilege, and the mask TLBLO_DIRTY is used to set it. The function `segment_is_readonly` is called to determine if the faultaddress is in the text segment and therefore is not writable.

<aside>
💡 In the dumbvm system, all of the address-space is writable. In our project, we changed this so that any attempt by a program to modify the text section results in the termination of the process. This is done in the TLB by the management of the dirty bit.

</aside>

```jsx
int tlb_insert(vaddr_t faultvaddr, paddr_t faultpaddr){
    /*faultpaddr is the address of the beginning of the physical frame, so I have to remember that I do not have to 
    pass the whole address but I have to mask the least significant 12 bits*/
    int entry, valid, is_RO; 
    uint32_t hi, lo, prevHi, prevLo;
    is_RO = segment_is_readonly(faultvaddr); // boolean that tells me if the address is read_only and therefore the dirty bit has to be set
    
    /*step 1: look for a free entry and update the corresponding statistic (FREE)*/
    for(entry = 0; entry <NUM_TLB; entry++){
        valid = tlb_entry_is_valid(entry);
        if(!valid){
            /*I can write the fault address here!*/
                hi = faultvaddr;
                lo = faultpaddr | TLBLO_VALID;
                /*is the segment a text segment?*/
               if(!is_RO){
                    /*I have to set a dirty bit (that is basically a write privilege)*/
                    lo = lo | TLBLO_DIRTY; 
                }
                tlb_write(hi, lo, entry);
            /*update the statistic "tlb fault free"*/
            add_tlb_type_fault(FAULT_W_FREE); //do I have to add the general faults as well or do I do it earlier?
            /*return*/
            return 0;
        }

    }
    /*step 2: I have not found an invalid entry. so... look for a victim, overwrite and update the correspnding statistic (REPLACE)*/
    entry = tlb_victim();
    hi = faultvaddr;
    lo = faultpaddr | TLBLO_VALID; //the entry has to be set as valid
    /*is the segment a text segment?*/
    if(!is_RO){
        /*I have to set a dirty bit that is basically a write privilege*/
        lo = lo | TLBLO_DIRTY; 
    }
    /*before overwriting the entry, I have to save the current content somewhere so that I can notify the page table of the replacement. The page
    table, indeed, keeps trace of its entries that are cached (in the TLB)*/
    tlb_read(&prevHi, &prevLo, entry);
    /*notify the pt that the entry with that virtual address is not in tlb anymore*/
    update_tlb_bit(prevHi, curproc->p_pid);
    /*Now I can overwrite the content*/
    tlb_write(hi, lo, entry);
    /*update tlb faults replace*/
    add_tlb_type_fault(FAULT_W_REPLACE);
    return 0;

}
```

### tlb_invalidate_all

<aside>
💡 In OS161, the global and pid fields are unused. Therefore, when there is a context switch, all of the entries of the TLB have to be invalidated.

</aside>

The invalidation of all of the TLB entries is performed by the `tlb_invalidate_all`.

This function first checks if the pid of the running process has changed with respect to the last time an invalidation has occurred. if so, it proceeds with writing the invalidating values in all of the TLB entries.

Before invalidating each entry, the TLB first informs the Page Table that the entry will not be cached anymore (this is performed by using the function “update_tlb_bit”, as we did in “tlb_insert”).

```jsx
void tlb_invalidate_all(void){
    uint32_t hi, lo;
    pid_t pid = curproc->p_pid; // I extract the pid of the currently running process
    if(previous_pid != pid) // the process (not the thread) changed. This is necessary because as_activate is called also when the thread changes.
    {
    DEBUG(DB_VM,"NEW PROCESS RUNNING: %d INSTEAD OF %d\n",pid,previous_pid);

    /*I update the correct statistics*/
    add_tlb_invalidation();

    /*I iterate on all the entries*/
    for(int i = 0; i<NUM_TLB; i++){
            if(tlb_entry_is_valid(i)){ // If the entry is valid
                tlb_read(&hi,&lo,i); // retrieve the content
                update_tlb_bit(hi,previous_pid); // I inform the Page Table that the entry identified by the pair (vaddr, pid) will not be "cached" anymore
            }
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i); // I override the entry
            }
    previous_pid = pid; // I update the global variable previous_pid so that the next time that the function is called I can determine if the process has changed.
    }
}
```

## Version 1 differences with respect to Version 2

The main optimization of the first version of our project with respect to the final one relies in `tlb_invalidate_all`. This function is called by *as_activate* each time there is a context switch. 

We noticed that one of the reasons of inefficiency in our initial solution was that the *as_activate* function is called all the times a new thread is created. We became aware of this behavior by looking at the statistics at the end of “*testbin/palin*”: an invalidation had been performed for each character printed on the console.

We improved this behavior by inserting a comparison between the pid of the running process with respect to the pid of the process that was in execution at the time of the last invalidation, as explained in the previous paragraph.

### Additional information

<aside>
❗ The utility functions `tlb_read`, `tlb_writes` and the masks such as ***TLBLO_VALID*** and ***TLBLO_DIRTY*** are defined in tlb.h

</aside>

# Inverted Page Table

The type of page table used is the Inverted page table, so it receives generally a virtual address and a process ID and stores those values in RAM. For finding a victim, we use a FIFO replace algorithm with second chance. If the page is not in the TLB and if Reference Bit is equal to 0 we can replace. If reference bit is 1 we set it to 0 s.t. we can use it as a circular buffer.

For the second-chance algorithm we consider only the pages that can be removed, or in other words we ignore the pages allocated with kmalloc, involved in an I/O operation, involved in a fork or currently inserted in the TLB. Lastly, when we remove an entry from the TLB we set its reference bit to 1.

This is due to the fact that TLB pages are the most recently accessed ones, and so it would be inefficient to select them as a victim since we’ll probably use them in the near future. The I/O constraint instead is necessary since otherwise the I/O operation will fail. In the same way, kmalloc pages can’t be moved since they are in kernel address space, that doesn’t access the page table to translate the virtual address into a physical one, so we can’t move them. Lastly, the swap bit is necessary to perform the fork correctly. In fact, the pages involved in a fork can’t be moved out from the page table in the middle of the copy, or otherwise we may have inconsistencies that could lead to pages not copied. To avoid this issue we freeze the situation from the beginning of fork until when it has been successfully completed.

The code related to this part is inside:

```jsx
kern/vm/pt.h  
kern/vm/pt.c  
```

## Version 1:  basic IPT

### pt.h

It contains all the names of the functions and also some structs that we will describe here.

We defined how the page table entry should be. Inside `pt_entry` we defined the virtual page `vaddr_t page`, the process ID  `pid_t pid`, and some control bits `uint8_t ctl` where, starting from the lower bit, we have

- validity bit
- reference bit
- TLB bit (it signals if the entry is in TLB or not)
- IO bit (it signals if the page is currently involved in a I/O operation with the disk or not)
- swap bit (it signals if the page is involved in an as_copy operation or not)

Then we created our Inverted Page Table and we added some informations about it inside the structure `ptInfo` . Our IPT is an array of entries `struct pt_entry *pt` and we also save the size of the array `int ptSize` , the first free physical address `paddr_t firstfreepaddr` , a lock `struct lock *pt_lock`, a condition variable `struct cv *pt_cv` and an array that signals in which part the array contains contiguous memory `int *contiguous` .

The last variable defined here is `pt_active` , that says if the page table is active (1) or not (0).

Then there are all the function declarations described in `pt.c` 

### pt.c

It contains all the functions used by the page table. Since we use a FIFO, a variable `int lastIndex` is defined (initially zero). The functions are:

- `pt_init`**************:************** it is called in the bootstrap and it is used to initialize the page table. It computes the number of frames that we can use inside RAM in order to allocate the size of our page table, then it creates the lock, the cv and the “contiguous array” defined before. It also initializes the page table with all invalid entries (using bit manipulations in order to set valid bit equals to 0) and “contiguous” array entries to -1. Then we save the first free physical address using function `ram_stealmem(0)` (that is used to “retrieve” 0 bytes from RAM, returning the first physical address)  and we set the page table as active.
- `pt_get_paddr` ****(1):****  It receives a virtual address and a process ID and returns the position (an integer) of the page in the array. It iterates on the page table to find the right index, else it returns -1. When a page is found, it sets TLB bit to 1.
- `findspace` **********(2):********** it finds a free spot iterating on all the page table; the entry is free if the validity bit is 0 and it’s not a kmalloc page, else it returns -1.
- `find_victim` ****(3):**** it finds a victim inside the page table. It iterates on the page table (also more than one cycle if all reference bits are 1). If the entry is not in the TLB and the reference bit is 0 it sends the entry inside the swap-space, saves in the page table virtual address and process ID and updates the next index `lastIndex` for the FIFO, then returns the index of the found victim.
- `get_page` ****:**** this functions is a huge wrapper that includes other created functions:
    1. It calls ********(1)******** in order to obtain the index of the virtual address for a given process ID.
    2. If the result is different from -1 it sets the TLB bit and returns the physical position.
    3. If the result is -1 it finds a free space with ********(2)********. 
    4. If the result of (2) is different from -1 it saves all the informations inside the page table, calculates the physical address and sets TLB bit, validity bit and IO bit to one.
    5. If the result is -1 it finds a victim using ********(3)********, then it calls `load_page` , defined in `segments.c` passing virtual address, process ID and physical address in order to load the provided physical address. 
    
    Please notice that IO bit is set when we find a victim or a free page and it’s cleared only when the load operation has successfully been completed. This is done to avoid any problem that may rise due to concurrency. For the same reason, also the TLB bit is set only when the load operation has been completed. For further details, please check the code and the related comments.
    
- `free_pages`**:** it receives a process ID and it iterates on the page table, removing all the entries that match with the PID. Then, it sets the control bits, the PID and the virtual address to 0.
- `update_tlb_bit`**:** if a page is removed from the TLB (it has been replaced) the TLB bit must be updated inside the page table. This function finds the match inside the page table and sets it to 0. It also sets the reference bit to 1 for the second chance algorithm.
- `get_contiguous_pages`**:** it is used to allocate kmalloc pages, that must be contiguous in memory (kernel) and can never be moved. It does a first iteration, trying to find a large enough invalid interval in order to insert the pages. If it’s not found, it performs other 2 loops, trying to swap out some pages with second chance algorithm. If we don’t get a suitable interval again, it waits on a condition variable. In fact, as long as the process is running, since we don’t do any I/O operation, no other process will be able to run (we disabled the interrupts), and thus the page table will never change. Instead, if we wait we allow other processes to run, that may free some entries allowing us to properly complete the kmalloc.
- `free_contiguous_pages`**:** it frees the contiguous pages allocated before (kernel).

## Version 2: Inverted page table with Hash Anchor Table (HAT) and fork implementation

All the functions that are not mentioned here, are unchanged with respect to the old version.

The optimization used here is called Hash Anchor Table (or at least it’s very resembling to it ). It involves an hash table with some entries ( in our case 2 times the page table size).  Each entry is a linked list (to handle collisions). When there is a page request inside the table, it accesses in the right hash table entry, using an hash function, and scans it with complexity O(C), where C<<page table size. The matching entry contains an index, that can be used to access the IPT with O(1) in order to retrieve the physical address.

Some other functions are added in order to support the fork operation. When a fork is executed, all the entries with a specified process ID must be copied with a new one. To enforce consistency, we set the swap bit for all the entries of the old PID at the beginning (so that they can’t be selected as victim), and we clear it when the fork ends.

### pt.h

**********************Code added:**********************

Two structs and a list have been added: 

- `struct hashentry` ****:**** it is a single element of each list. It contains the process ID, the virtual address and the position inside the IPT. Furthermore, since it’s a list, it also contains the pointer to the next entry.
- `struct hashT` ****:**** it’s the real hash table, composed of an array of pointers to the previously defined structures. It also contains the number of entries of the hash table.
- `struct hashentry *unusedptrlist` ****:**** it’s a list of free pointers that we can use when adding something in the hash table.

### pt.c

********************************Added functions:********************************

- `htable_init` **:** first it defines the size of the hash table, then it allocates the table, then each entry is set to NULL. The free pointer list ( `unusedptrlist`) is initialized with as many structures as the page table size.
- `get_hash_func` ****:**** it creates an hash function using the given virtual address and the process ID; it uses some modules, bit manipulation and XOR (useful to randomize everything).
- `add_in_hash` ****:**** it inserts in the hash table a new element: first it calculates the position in the HT using the hash function, then the new element is initialized and attached with an insertion in head.
- `get_index_from_hash` **:** it is used to return the right IPT index using the hash table; if it is not found, it returns -1;
- `remove_from_hash` ****:**** given the virtual address and process ID, it removes an element from the list in order to attach it again in `unusedptrlist`.
- `copy_pt_entries`**:** this function is used for forking, so copying all the pages related to a process ID for a new one. If there is a free space (pages with valid bit = 0) the page can be copied into that entry, otherwise we store it in the swap file.
- `prepare_copy_pt` and `end_copy_pt` are two functions used for fork set-up, saying that there will be a swap (SWAP bit=1) in the case of the **************prepare**************, or telling that the operation is finished (SWAP bit=0) in the case of ******end******.

**********************Modified functions:**********************

- `find_victim` ****:**** when a page must be removed, it also removes the entry from the hash table. Then it adds the new virtual page and the new process ID inside the hash table.
- `get_page` : when a space inside the IPT is found, it must also add the element inside the hash table in the correct position (using `add_in_hash`).
- `pt_get_paddr`: now the value is retrieved from the hash table and not from the IPT.
- `free_pages`: it calls now inside the loop the function `remove_from_hash`. For each page removed from the IPT, we remove it from the hash table too.

# ADDRSPACE

<aside>
💡 All the functions described here can be found in `kern/vm/addrspace.c`, `kern/include/addrspace.h` and in `kern/syscall/loadelf.c`

</aside>

To support demand paging, we must perform some modifications to address space handling. The first change is related to how the stack is treated. In fact, in DUMBVM currently we immediately allocate a contiguous memory area for the stack, with size = `DUMBVM_STACKPAGES * PAGESIZE`. Instead, with demand paging we want to load them on demand, so we don’t allocate anything.

The second issue is that, currently, when we want to run a program we immediately load the whole executable in physical memory. Again this is a behavior that must be changed. To solve it, in `load_elf` we’ll just define the virtual address space.

However, we’ll need to access the ELF file later on to load pages that haven’t been mapped yet into frames. To deal with it, we must store in the struct addrspace the program headers related to the text segment and the data segment. Of course, it won’t be necessary for the stack since it’ll be completely handled at runtime (or, in other words, when we access a new stack page we just zero-fill the accessed frame). We must save the vnode related to the executable too, otherwise we won’t be able to read it in the future.

There’s however another issue with the address space management. In fact, as we’ve seen we save in the address space `as->as_vbase` and `as->as_npages`. However, the actual starting virtual address may not be aligned to a page. Since this information is lost in `as->as_vbase` (that is aligned to a page), we must create an additional field for each segment, that we called `initial_offset`. We’ll analyze how to use it in segments section.

Lastly, when a process ends we clear all its entries from the page table and the swapfile, to avoid potential memory leaks that could cause issues in the future. We also provide support to fork with the function `as_copy`, that copies all the pages of the old process in the page table and in the swapfile for the new process too.

# SEGMENTS

<aside>
💡 All the functions described here can be found in `kern/vm/segments.c` and in `kern/include/segments.h`

</aside>

The goal of segments is to properly handle page faults. When they occur, we need to load a page in the provided physical address. The loading may occur either from the ELF file (on the first access to a certain page) or from the swapfile (when we already loaded it from the ELF file and we needed to store it in the swapfile). 

Due to this, in the function `load_page` we firstly check if the page is present in the swapfile. If not, it means that we never accessed it, and so we must load it from the ELF file. 

To perform correctly the ELF load, we firstly identify to which segment it belongs. If it is a text or data page we load it (with the `load_elf_page` function), while if it is a stack access we just zero-fill the page.

To be able to load from the ELF, we store in the address space data structure the program header related to the text and data segment. In this way, we are able to compute the offset within the file in the following way

```c
offset = as->ph.offset + (vaddr - as->ph.as_vbase);
```

In fact, vaddr is the starting address of the accessed page, while vbase is the starting address of the given segment.

<aside>
❗ Notice that it’s not correct to assume that the process will always access the pages within the ELF file sequentially. In fact, we could have jumps in the text segment or we could access the variables with a different order with respect to declaration order, so we can’t assume a sequential access.

</aside>

Even if we don’t read sequentially the ELF file, we’re sure that we’ll read each page at most once (since if we already read it, it will be found in the page table or in the swapfile, that are accessed before the ELF file).

<aside>
❗ Another aspect that must be taken into account is the difference between filesz and memsz. The best way to understand it is to debug the structure of the ELF file while executing `testbin/zero`. If you don’t handle this specific case you might get in troubles.

</aside>

To solve it, when we access a page we compute

```c
sz = as->ph.filesz - (vaddr - as->ph.as_vbase);
```

In this way, sz will store the number of bytes that we must effectively load from the file. If sz>`PAGE_SIZE` we assign sz=`PAGE_SIZE`, while if sz<0 we just zero-fill the page and we return. In fact, this is the case in which memsz>filesz, that happens when you declare variables but you don’t initialize them (like in `testbin/zero` test). Lastly, if 0<sz<`PAGE_SIZE` we’ll zero-fill the page before loading sz bytes (this can happen if the last page has internal fragmentation).

There’s a third case to take into account. As we anticipated in addrspace section, in some cases the starting virtual address may not be aligned to a page. In this case, we must consider the initial offset when we access the first page (the offset can’t be >`PAGE_SIZE`, otherwise `as→as_vbase` would be different). If, for example, for the process the starting virtual address is 0x412100, if we don’t consider the initial offset of 0x100 we’ll start loading data from 0x412000, while the process expects it to start from 0x412100. To solve this issue, if initial_offset≠0 and we access the first page, we must zero-fill the page (so that the first initial_offset bits will be 0) and then start loading data from the position initial_offset within the block (and not 0 as usual).

For a deeper understanding, I firstly suggest you to debug `testbin/bigfork` with DUMBVM and to compare the differences with your VM system. Then, analyze carefully the code and the comments in the function `load_page` in `segments.c`.

# SWAPFILE

<aside>
💡 All the functions described here can be found in `kern/vm/swapfile.c` and in `kern/include/swapfile.h`

</aside>

## V1

In the first version of the swapfile, we mainly figured out how to make it work, completely ignoring efficiency (that will be improved in V2).

The simplest design choice is to create a struct (`swap`), that contains a  vnode (to operate on the ELF file), a size (i.e. the maximum number of pages that can be contained in the swapfile) and the array of stored pages (each one containing the PID and the virtual address related to the stored page). 

The three main operations performed are initialization (allocation of data structure and opening of the swapfile), load and store. Store is called every time a page is swapped out from the page table, while load every time we have a page fault (it’s called indirectly by `load_page` in segments). Reading/writing operations are very similar to the ones implemented for the ELF file, as well as the logic to properly handle the offset.

To support fork instead, we simply copy all the pages of the old process stored in the swap into free pages, that will be assigned to the new process. To do it we store the old page into a kernel buffer, allocated with kmalloc, and then we store the kernel buffer into the new page.

Lastly, when a process ends we simply mark all its pages as free. We don’t need to explicitly zero-fill them since they’ll be overwritten by other processes.

<aside>
❗ V1 is working only for programs that don’t use fork. In other words, we didn’t implement synchronization mechanisms, that have been developed instead for V2.

</aside>

## V2

Currently, the first solution has some sub-optimizations, that mainly come from the fact that, since we operate on an array, all the operations have a linear complexity. It is true that, with swap space, the main cost is provided by I/O, but on the other hand it may not be a bad idea to optimize search operations.

Due to this, we decided to transform the array into linked lists. We implemented a linked list of free frames, common to all the processes, and three arrays of linked lists (one for text segments, one for data segments and one for stack segments), with size=`MAX_PROC` (i.e. the maximum number of PIDs allowed). In this way, each process has its own list for each segment, and once we determined the segment accessed by a certain virtual address we can search for the corresponding entry in the swap file by analyzing a small subset of all the frames. 

For all the operations, we performed insertion/removal from head. Please notice that a precise order of the operations must be enforced to avoid issues with concurrency. To have further details, please analyze the functions and their comments.

Again, load and store are very similar to the ones used for the ELF file. The main difference is that, if we try to load a page that’s currently being stored, we wait until when the store operation has been completed to avoid errors with I/O operations. For this purpose, we introduced for each `swap_cell` (i.e. the elements representing a page in the swapfile) a flag (`stored`), a condition variable and a lock.

To support fork, we duplicate all the swap pages belonging to the old PID and we assign them to the new one. Again, for implementation details please analyze the functions and their comments. Lastly, when a process ends we inserts all its `swap_cell` from the three lists into the free list.

Since in all the tests we never used a big portion of the swapfile, there’s a slight sub-optimization that, in case of intensive use of the swap space, may lead to errors. Currently, as you can see in the code, when we load a page from swap to RAM firstly we remove the page from the list of the process, then we perform the load operation and lastly we put back the frame into the free list. Due to this, during I/O one page is dangling, and although it will be free in the future it doesn’t appear in the free list. If, in the meanwhile, there are no free pages left and a process needs to store something in the swap, we’ll have a panic although theoretically we could have a free page if we wait.

We didn’t address this problem since it would cause an overhead in the search for a free frame and since we assumed that the swapfile is big enough to avoid this situation. Potential solutions are the introduction of a load flag (similar to the store flag but used to understand if a free page can be used or if we must wait) or the creation of another list, that stores the frames that currently are involved in a load but that will become free in the future. If the pointer of the free list is NULL but the pointer of this latter list is not NULL, we understand that we simply have to wait to get a free page.

We also introduced a slight optimization. When a process ends, the pages in the free list will have a random order for the offset field, that depends on the program execution. Since this field causes an overhead (the higher the offset the slower the I/O operation), we decided to reorder the offset field after the end of the program. In this way, we won’t see a decrease in performance when we execute multiple programs in sequence.
