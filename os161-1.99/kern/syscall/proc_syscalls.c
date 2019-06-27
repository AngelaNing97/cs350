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
#include <synch.h>
#include <machine/trapframe.h>
#include "opt-A2.h"

// hold the handlers for process-related system calls
// TODO: add handlers for the 4 ops

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  #if OPT_A2 
    lock_acquire(procLock);
    if (curproc->pid != 6) { //not kernel proc
      int set_ret = setProcExitCode(p->pid, exitcode);
      if (set_ret != 0 ) {
        panic("process does not exist in proc table\n");
      }
      cv_broadcast(procExitCV, procLock);
    }
    lock_release(procLock);
  #else
  (void)exitcode;
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
  #if OPT_A2 
  *retval = curproc->pid;
  #else
  *retval = 1;
  #endif /* OPT_A2 */
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

  if (options != 0) {
    return(EINVAL);
  }
  
  
  #if OPT_A2 
  lock_acquire(procLock);
  // check if curproc is parent of this pid
  if ((int)curproc->pid != 6) { //not kernel proc
    bool isChild = false;
    struct array *childrenProcs = curproc->childrenProcsIds;
    for (unsigned int i = 0; i < array_num(childrenProcs); i++) {
      pid_t child_pid = * (pid_t *)array_get(childrenProcs, i);
      if (child_pid == pid) {
        isChild = true;
        break;
      }
    }
    if (!isChild) {
      lock_release(procLock);
      return ECHILD; //cannot call waitpid if it's not your child
    }
  }
  
  struct procTableEntry *pte = getProcTableEntry(pid);
  if (pte == NULL) {
    lock_release(procLock);
    return ESRCH;
  }

  // if (pte->exit_code != -6) { // if waitpid is called after the child process has exited
  //   exitstatus = pte->exit_code;
  // } else { // if waitpid is called before the child exits
  // }
  while(pte->exit_code == -6) {
    cv_wait(procExitCV, procLock);
  }
  exitstatus = pte->exit_code;
  removeProcFromTable(pid);
  // removeProcFromParent(pid); //dont REALLY need to remove the proc from parent
  lock_release(procLock);

  #else
  exitstatus = 0;
  #endif /* OPT_A2 */
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#if OPT_A2
int
sys_fork(struct trapframe *tf, pid_t *retval)
{
  // 1. create proc structure for child process
  struct proc *child_proc = proc_create_runprogram("childProc");
  if (child_proc == NULL) {
    return ENPROC;
  }

  // 2. create and copy address space (and data) from parent to child
  struct addrspace *as = kmalloc(sizeof(struct addrspace));
  if (as == NULL) {
    proc_destroy(child_proc);
    return ENOMEM;
  }
  int as_ret = as_copy(curproc_getas(), &as);
  if (as_ret != 0) {
    proc_destroy(child_proc);
    return as_ret;
  }

  // 3. attach the newly created addr space to the child proc structure
  spinlock_acquire(&child_proc->p_lock);
  child_proc->p_addrspace = as;
  spinlock_release(&child_proc->p_lock);

  // 4. assign pid to child process and create the child/parent relationship

  lock_acquire(procLock);
  child_proc->ppid = curproc->pid;
  if (curproc->pid >= 7) {
    pid_t *pidp = kmalloc(sizeof(pid_t));
    *pidp = child_proc->pid;
    array_add(curproc->childrenProcsIds, pidp, NULL);
  }
  // counter++;
  // child_proc->pid = (pid_t) counter;
  // // child_proc->parent = curproc;
  // // array_add(curproc->childrenProcs, child_proc);
  // addToProcTable(child_proc->pid, curproc->pid);
  lock_release(procLock);

  // 5. create thread for child process, need a safe way to pass the trapframe to the child thread
  struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
  if(child_tf == NULL){
    proc_destroy(child_proc);
    return ENOMEM;
  }
  *child_tf = *tf; // put curproc tf on the heap

  // 6. chidl thread needs to put the trapframe on the stack, modify it so it returns the correct values
  // 7. call mips_usermode in the child to go back to userspace
  int thread_fork_res = thread_fork("child_thread", child_proc, (void *)enter_forked_process, child_tf, 0);
  // kfree(child_tf);
  if (thread_fork_res != 0) {
    proc_destroy(child_proc);
    return thread_fork_res;
  }

  *retval = child_proc->pid;
  return 0;
}
#endif /* OPT_A2 */

