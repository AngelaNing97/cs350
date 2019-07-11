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
#include <vfs.h>
#include <kern/fcntl.h>

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
      
      for (unsigned int i = 0; i < array_num(procTable); i++) { //change all child process's ppid to -1
        struct procTableEntry *pte = array_get(procTable, i);
        if (pte->ppid == pte->pid) {
          pte->ppid = -1;
        }
        if (pte->ppid == -1 && pte->exit_code != -6) { //proc has no living parent and has finished
          removeProcFromTable(pte->pid);
        }
      }

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
    for (unsigned int i = 0; i < array_num(procTable); i++) {
      struct procTableEntry *pte = array_get(procTable, i);
      if (pte->pid == pid && pte->ppid == curproc->pid) {
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

  while(pte->exit_code == -6) {
    cv_wait(procExitCV, procLock);
  }
  exitstatus = pte->exit_code;
  removeProcFromTable(pid);
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
    setProcPPid(child_proc->pid, curproc->pid);
  }
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

int sys_execv(char *program, char **args) {
  // (void) args; //for now, no args passing

  //credit: copied from runprogram
  struct addrspace *as;
  struct vnode *v;
  vaddr_t entrypoint, stackptr;
  int result;
  int ac = 0;

  /* get the program path */
  if (program == NULL) {
    return EFAULT;
  }
  size_t progname_size = strlen(program)+1;
  char *progname = kmalloc(progname_size * sizeof(char));
  if (progname == NULL) {
    return ENOMEM;
  }
  int copyname_res = copyinstr((const_userptr_t) program, progname, progname_size, NULL);
  if (copyname_res) {
    kfree(progname);
    return copyname_res;
  }

  //TODO: count the number of arguments and copy them into the kernel
  while (args[ac] != NULL) {
    ac++;
  }
  char **programArgs = kmalloc((ac+1) * sizeof(char *));
  if (programArgs == NULL) {
    kfree(progname);
    return ENOMEM;
  }
  programArgs[ac] = NULL; //null-terminated

  for(int i = 0; i < ac; i++) {
    size_t length = strlen(args[i]) + 1;
    programArgs[i] = kmalloc(sizeof(char) * length);
    result = copyinstr((userptr_t)args[i], programArgs[i], length, NULL);
  }

  // kprintf("number of args: %d \n", ac);
  // for(int i = 0; i < ac; i++) {
  //   kprintf(programArgs[i]);
  //   kprintf("\n");
  // }
  // if (programArgs[ac]!=NULL) {
  //   panic("argument array not null-terminated\n");
  // }

  /* Open the file. */
  result = vfs_open(progname, O_RDONLY, 0, &v);
  if (result) {
    return result;
  }

  KASSERT(curproc_getas() != NULL);
  struct addrspace *oldas = curproc_getas();

  /* Create a new address space. */
  as = as_create();
  if (as ==NULL) {
    vfs_close(v);
    return ENOMEM;
  }

  /* Switch to it and activate it. */
  curproc_setas(as);
  as_activate();

  /* Load the executable. */
  result = load_elf(v, &entrypoint);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    vfs_close(v);
    return result;
  }

  /* Done with the file now. */
  vfs_close(v);

  /* Define the user stack in the address space */
  #if OPT_A2 
  result = as_define_stack(as, &stackptr, programArgs, ac);
  #else
  result = as_define_stack(as, &stackptr);
  #endif /* OPT_A2 */
  
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    return result;
  }

//TODO: copy the arguments, both the array and the strings, onto the user stack
  //1. copy the strings onto user stack
  // vaddr_t currStack = USERSTACK;
  // vaddr_t argStackAddr[ac+1];
  // argStackAddr[ac] = 0;

  // for (int i = ac-1; i >= 0; i--) { // put the strings onto the stack
  //   size_t decrement = strlen(programArgs[i]) + 1;
  //   stackptr -= decrement;
  //   argStackAddr[i] = stackptr;
  //   result = copyoutstr(programArgs[i], (userptr_t)stackptr, decrement, NULL);
  // }

  // // align the stack ptr to be 4 byte aligned
  // while (stackptr % 4 != 0) {
  //   stackptr -= 1;
  // }

  // for (int i = ac; i >=0; i--) { // put the ptr onto the stack
  //   stackptr -= ROUNDUP(sizeof(vaddr_t), 4);
  //   result = copyout(&argStackAddr[i], (userptr_t)stackptr, 4);
  // }

  //delete old address space
  as_destroy(oldas);

//TODO: call enter_new_process with address to the arguments on the stack, the stack ptr, and the program entry point
  /* Warp to user mode. */
  // enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
  //       stackptr, entrypoint);
  enter_new_process(ac /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
        stackptr, entrypoint);
  
  /* enter_new_process does not return. */
  panic("enter_new_process returned\n");
  return EINVAL;

}
#endif /* OPT_A2 */

