/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <mips/trapframe.h>
#include <current.h>
#include <synch.h>
#include "swapfile.h"
#include "pt.h"
#include "opt-project.h"

/*
 * system calls for process management
 */
void
sys__exit(int status)
{
  struct proc *p = curproc;

  #if OPT_PROJECT
  remove_process_from_swap(p->p_pid);
  free_pages(p->p_pid);
  struct addrspace *as = proc_getas();
  vfs_close(as->v);
  #endif

  p->p_status = status & 0xff; /* just lower 8 bits returned */
  proc_remthread(curthread);

  lock_acquire(p->lock);
  cv_signal(p->p_cv, p->lock);
  lock_release(p->lock);
  thread_exit();

  panic("thread_exit returned (should not happen)\n");
}

int
sys_waitpid(pid_t pid, userptr_t statusp, int options)
{
  struct proc *p = proc_search_pid(pid);
  int s;
  (void)options; /* not handled */
  if (p==NULL) return -1;
  s = proc_wait(p);
  if (statusp!=NULL) 
    *(int*)statusp = s;
  return pid;
}

pid_t
sys_getpid(void)
{
  KASSERT(curproc != NULL);
  return curproc->p_pid;
}