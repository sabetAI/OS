#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <queue.h>
#include <opt-A2.h>
#include <mips/trapframe.h>
#include <synch.h>


#if OPT_A2
pid_t sys_fork(struct trapframe *parent_tf, pid_t *retval){
    /* Create new process struct */
    struct proc *child_proc = proc_create_runprogram(curproc->p_name);
    if (child_proc == NULL){
        DEBUG(DB_SYSCALL, "proc_create_runprogram() failed in sys_fork()\n");
        // does ptable entry never add in this case?
        return ENPROC;
    }
    /* copy old address spaces to new one */
    struct addrspace *newas[1];
    if (as_copy(curproc_getas(), newas) != 0){
        DEBUG(DB_SYSCALL, "as_copy() out of memory in sys_fork()!\n");
        lock_acquire(ptable_lock);
        remove_pt_entry(child_proc->pid);
        lock_release(ptable_lock);

        proc_destroy(child_proc);
        return ENOMEM;
    }

    child_proc->p_addrspace = *newas; // don't need to use lock because not shared
   
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL){
        DEBUG(DB_SYSCALL, "couldn't create trapframe in sys_fork().\n");
        lock_acquire(ptable_lock);
        remove_pt_entry(child_proc->pid);
        lock_release(ptable_lock);

        proc_destroy(child_proc);
        return ENOMEM;
    }

    // synch issues?
    memcpy(child_tf, parent_tf, sizeof(struct trapframe));

    int exit_status = thread_fork(curproc->p_name, child_proc, enter_forked_process, child_tf, -1);
    if(exit_status){
        DEBUG(DB_SYSCALL, "thread_fork() fail in sys_fork()");

        lock_acquire(ptable_lock);
        remove_pt_entry(child_proc->pid);
        lock_release(ptable_lock);

        proc_destroy(child_proc);
        kfree(child_tf);
        return exit_status; 
    }

    *retval = child_proc->pid;

    return 0;
}

#endif /* OPT_A2 */


  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */

#if OPT_A2
  lock_acquire(ptable_lock);
  struct pt_entry *pt_curr = get_ptable_entry(curproc->pid);
  if (pt_curr == NULL){
      DEBUG(DB_SYSCALL, "ptable entry missing for currproc in call sys__exit\n");
      // do stuff
  } 

  if (pt_curr->parent_pid == PID_ORPHAN){
   remove_pt_entry(curproc->pid);
   update_pt_children(curproc->pid);
  } else {
   // check if process exited or was signalled/stopped
   if (WIFEXITED(exitcode)){
    pt_curr->exit_status = _MKWAIT_EXIT(exitcode);
   } else if (WIFSIGNALED(exitcode)) {
    pt_curr->exit_status = _MKWAIT_SIG(exitcode);
   } else { // WIFSTOPPED
    pt_curr->exit_status = _MKWAIT_STOP(exitcode);
   }
   pt_curr->status = S_ZOMBIE;
   update_pt_children(curproc->pid);
   cv_broadcast(ptable_cv, ptable_lock);
  }
  lock_release(ptable_lock);

#endif /* OPT_A2 */

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = curproc->pid;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  lock_acquire(ptable_lock);
  struct pt_entry *pt_child = get_ptable_entry(pid);

  if (options != 0) {
    lock_release(ptable_lock);
    return EINVAL;
  } else if (pt_child == NULL) {
    lock_release(ptable_lock);
    return ESRCH;
  } else if (pt_child->parent_pid != curproc->pid){
    lock_release(ptable_lock);
    return ECHILD;
  } else if (status == NULL){
    lock_release(ptable_lock);
    return EFAULT;
  }

  while(pt_child->status == S_RUN){
      cv_wait(ptable_cv, ptable_lock);
  }


  /* for now, just pretend the exitstatus is 0 */
  exitstatus = pt_child->exit_status;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

