#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "proc_list.h"


struct cpu cpus[NCPU];
uint64 kstacks[NPROC_KSTACK];
int free_kstacks[NPROC_KSTACK];
struct spinlock kstacks_lock;

struct proc_list *proc_list;
struct spinlock proc_list_lock;

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

pagetable_t kpgtbl_local;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  for (int idx = 0; idx < NPROC_KSTACK; idx++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK(idx);
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    kstacks[idx] = va;
  }
  memset(free_kstacks, 1, sizeof(free_kstacks));
  initlock(&kstacks_lock, "kstacks lock");

  kpgtbl_local = kpgtbl;
}

// initialize the proc table.
void
procinit(void)
{ 
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  proc_list = bd_malloc(sizeof(struct proc_list));
  proc_lst_init(proc_list);
  initlock(&proc_list_lock, "prock_list_loc");
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc_list *cur_list;
  struct proc* p;

  cur_list = bd_malloc(sizeof(struct proc_list));
  p = bd_malloc(sizeof(struct proc));
  cur_list->cur_proc = p;
  // No memory for new proc
  if (p == 0 || cur_list == 0) {
    return 0;
  }
  initlock(&p->lock, "proc");
  acquire(&p->lock);
  p->pid = allocpid();

  // Use prealloced kstacks if possible
  uint64 va;
  int found_va = 0;
  acquire(&kstacks_lock);
  for (int idx = 0; idx < NPROC_KSTACK; idx++) {
    if (free_kstacks[idx]) {
      va = kstacks[idx];
      free_kstacks[idx] = 0;
      found_va = 1;
      break;
    }
  }
  release(&kstacks_lock);
  if (!found_va) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");

    va = KSTACK(p->pid + NPROC_KSTACK);
    kvmmap(kpgtbl_local, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }

  // From procinit
  p->kstack = va;
  memset(p->ofile, 0, sizeof(p->ofile));
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0) {
    freeproc(p);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  // Some other fields
  p->killed = 0;
  p->chan = 0;
  p->xstate = 0;

  acquire(&proc_list->cur_proc->lock);
  proc_lst_push(proc_list, cur_list);
  release(&proc_list->cur_proc->lock);
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  release(&p->lock);
  struct proc_list *cur = get_proc_list_by_proc(p, proc_list);
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  
  acquire(&kstacks_lock);
  for (int idx = 0; idx < NPROC_KSTACK; idx++) {
    if (kstacks[idx] == p->kstack) {
      free_kstacks[idx] = 1;
      break;
    }
  }
  release(&kstacks_lock);

  proc_lst_remove(cur);
  kfree(p);
  kfree(cur);
  // printf("Freed proc with pid %d\n", p->pid);
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  printf("First user process: %p\n", p);
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);
  // printf("List proc size after fork with pid %d is %d\n", np->pid, proc_lst_size(proc_list));
  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;
  struct proc_list *cur_proc;

  for(cur_proc = proc_list->next; cur_proc != proc_list; cur_proc = cur_proc->next) {
    pp = cur_proc->cur_proc;
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  struct proc_list* cur_proc;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(cur_proc = proc_list->next; cur_proc != proc_list; cur_proc = cur_proc->next) {
      pp = cur_proc->cur_proc;
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            release(&proc_list_lock);
            return -1;
          }
          freeproc(pp);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct proc_list* cur_proc;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    acquire(&proc_list_lock);
    for(cur_proc = proc_list->next; cur_proc != proc_list; cur_proc = cur_proc->next) {
      p = cur_proc->cur_proc;
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;

        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
    release(&proc_list_lock);
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 2)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;
  struct proc_list* cur_proc;

  for(cur_proc = proc_list->next; cur_proc != proc_list; cur_proc = cur_proc->next) {
    p = cur_proc->cur_proc;
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;
  struct proc_list* cur_proc;

  for(cur_proc = proc_list->next; cur_proc != proc_list; cur_proc = cur_proc->next) {
    p = cur_proc->cur_proc;
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  struct proc_list* cur_proc;
  char *state;

  printf("\n");
  for(cur_proc = proc_list->next; cur_proc != proc_list; cur_proc = cur_proc->next) {
    p = cur_proc->cur_proc;
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

void print_registry(uint64 registry, int number)
{
  printf("Registry number %d: %d.\n", number, registry & 0xFFFFFFFF);
}

int dump(void)
{
  struct proc *cur_proc = myproc();
  struct trapframe *trapframe = cur_proc->trapframe;
  print_registry(trapframe->s2, 2);
  print_registry(trapframe->s3, 3);
  print_registry(trapframe->s4, 4);
  print_registry(trapframe->s5, 5);
  print_registry(trapframe->s6, 6);
  print_registry(trapframe->s7, 7);
  print_registry(trapframe->s8, 8);
  print_registry(trapframe->s9, 9);
  print_registry(trapframe->s10, 10);
  print_registry(trapframe->s11, 11);

  return 0;
}

int dump2(int pid, int register_num, uint64 return_value_addr){
  if (register_num < 2 || register_num > 11) return -3;

  struct proc *my_proc = myproc();
  struct proc_list* cur_proc;
  
  struct proc *p;
  struct proc *requested_proc = 0;

  for(cur_proc = proc_list->next; cur_proc != proc_list; cur_proc = cur_proc->next) {
    p = cur_proc->cur_proc;
    if (p-> state == UNUSED) continue;
    if (p->pid == pid){
      requested_proc = p;
      break;
    }
  }

  if (requested_proc == 0){
    return -2;
  }
  
  if (requested_proc->pid != my_proc->pid && requested_proc->parent->pid != my_proc->pid){
    return -1;
  }

  uint64 register_value;
  switch (register_num){
    case 2: 
      register_value = requested_proc->trapframe->s2;
      break;
    case 3: 
      register_value = requested_proc->trapframe->s3;
      break;
    case 4: 
      register_value = requested_proc->trapframe->s4;
      break;
    case 5: 
      register_value = requested_proc->trapframe->s5;
      break;
    case 6: 
      register_value = requested_proc->trapframe->s6;
      break;
    case 7: 
      register_value = requested_proc->trapframe->s7;
      break;
    case 8: 
      register_value = requested_proc->trapframe->s8;
      break;
    case 9: 
      register_value = requested_proc->trapframe->s9;
      break;
    case 10: 
      register_value = requested_proc->trapframe->s10;
      break;
    case 11: 
      register_value = requested_proc->trapframe->s11;
      break;
  }

  int res = copyout(my_proc->pagetable, return_value_addr, (char *) (&register_value), 8);

  if (res == -1){
    return -4;
  }

  return 0;
}
