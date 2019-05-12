#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "tournament_tree.h"
#include "kthread.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

//our code***
struct {
  struct spinlock lock;
  struct kmutex mutex[MAX_MUTEXES];
} mutex;
int nextmutex = 1;
//end of our code***

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  //p = c->proc;
  p = c->kthread->owner;
  popcli();
  return p;
}

//our code**********
struct kthread*
mykthread(){
  struct cpu *c;
  struct kthread *kt;
  pushcli();
  c = mycpu();
  kt = c->kthread;
  popcli();
  return kt;
}

struct spinlock* 
getptablelock(){
  return &ptable.lock;
}

struct spinlock* 
getmutexlock(){
  return &mutex.lock;
}
//end of our code****

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;
  int i = 0;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pstate == UNUSED)
      goto found;
    i++;
  }

  release(&ptable.lock);
  return 0;

found:
  p->pstate = EMBRYO;
  p->ppid = nextpid++;
  p->runnablethreads = 0;
  release(&ptable.lock);

  //our code******
  //reset all threads. stack memory free is at wait()
  for(i = 0 ; i < NTHREAD ; i++) {
    p->kthreads[i].state = UNUSED;
    p->kthreads[i].kstack = 0;
    p->kthreads[i].kpid = 0;
    p->kthreads[i].owner = 0;
    p->kthreads[i].context = 0;
    p->kthreads[i].tf = 0;
    p->kthreads[i].killed = 0;
    p->kthreads[i].name[0] = 0;
    p->kthreads[i].kchan = 0;
  }

  p->indexinptable = i;
  struct kthread *t = &(p->kthreads[0]);
  t->owner = p;
  t->state = EMBRYO;
  t->kpid = nextpid++;
  //add name for debug

  //end of our code***

  // Allocate kernel stack.
  if((t->kstack = kalloc()) == 0){
    p->pstate = UNUSED;
    return 0;
  }
  sp = t->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *t->tf;
  t->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *t->context;
  t->context = (struct context*)sp;
  memset(t->context, 0, sizeof *t->context);
  t->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  struct kthread *t;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  t = &(p->kthreads[0]);
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(t->tf, 0, sizeof(*t->tf));
  t->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  t->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  t->tf->es = t->tf->ds;
  t->tf->ss = t->tf->ds;
  t->tf->eflags = FL_IF;
  t->tf->esp = PGSIZE;
  t->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->pname, "initcode", sizeof(p->pname));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->pstate = RUNNABLE;
  t->state = RUNNABLE;
  p->runnablethreads++;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  acquire(&ptable.lock);  //needs to hold ptable lock for pipe maybe?
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  }
  curproc->sz = sz;
  switchuvm(curproc, mykthread());
  release(&ptable.lock);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();
  struct kthread *curthread = mykthread();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  acquire(&ptable.lock);
  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kthreads[0].kstack);
    np->kthreads[0].kstack = 0;
    np->kthreads[0].state = UNUSED;
    np->pstate = UNUSED;
    release(&ptable.lock);
    return -1;
  }
  np->sz = curproc->sz;
  np->pparent = curproc;
  *(np->kthreads[0].tf) = *(curthread->tf); //modified
  // Clear %eax so that fork returns 0 in the child.
  np->kthreads[0].tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->pname, curproc->pname, sizeof(curproc->pname));

  pid = np->ppid;

  np->pstate = RUNNABLE;
  //our code***
  np->kthreads[0].state = RUNNABLE;
  np->runnablethreads++;
  //end of our code***

  release(&ptable.lock);

  return pid;
}

//called by the last thread of a process that performs exit.
void
exitproc(){
  int fd;
  struct proc *p;
  struct kthread *t;
  struct kthread *curthread = mykthread();
  struct proc *curproc = myproc();

  for(t = curproc->kthreads; t < &curproc->kthreads[NTHREAD]; t++){
    if(t->kpid == curthread->kpid) {
      continue;
    }
    if (!(t->state == ZOMBIE || t->state == UNUSED)){
      panic("exitproc is called but not all other threads are dead!");
    }
    else if(t->state == ZOMBIE) {
      kfree(t->kstack);
      t->kpid = 0;
      t->owner = 0;
      t->context = 0;
      t->tf = 0;
      t->killed = 0;
      t->name[0] = 0;
      t->state = UNUSED;
    }
  }

  deallocallprocessmutexs();

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->pparent);
  //our code***
  wakeup1(curthread->kchan);
  //end of our code***

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pparent == curproc){
      p->pparent = initproc;
      if(p->pstate == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  //our code***
  curthread->state = ZOMBIE;
  //end of our code***
  curproc->pstate = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct kthread *curthread = mykthread();
  struct proc *curproc = curthread->owner;
  struct kthread *t;
  int last = 1;
  //set all threads kill field and check if last thread of the process
  acquire(&ptable.lock);
  for(t = curproc->kthreads; t < &curproc->kthreads[NTHREAD]; t++){
    t->killed = 1;
    if(t->state == SLEEPING){
      t->state = RUNNABLE;
      curproc->pstate = RUNNABLE;
      curproc->runnablethreads++;
    }
    if (t->kpid != curthread->kpid && !(t->state == ZOMBIE || t->state == UNUSED)){
      last = 0;
    }
  }

  if (last){
    //not returning from this call
    release(&ptable.lock);
    exitproc();
  }
  else{
    //thread will not run again
    curthread->state = ZOMBIE;
    wakeup1(curthread);
  }
  sched();
  panic("not supposed to be here!\n");
  release(&ptable.lock);
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  struct kthread *t;
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->pparent != curproc)
        continue;
      havekids = 1;
      if(p->pstate == ZOMBIE){
        // Found one.
        pid = p->ppid;
        //our code***
        for(t = curproc->kthreads; t < &curproc->kthreads[NTHREAD]; t++){
          if (t->state == ZOMBIE){
            kfree(t->kstack);
            t->kstack = 0;
            t->state = UNUSED;
            t->kpid = -1; //for debug
            //no need for wakeup(t->chan) because process is zombie
          }
        }
        //end of our code***

        //kfree(p->kstack);
        //p->kstack = 0;
        freevm(p->pgdir);
        p->ppid = 0;
        p->pparent = 0;
        p->pname[0] = 0;
        p->pstate = UNUSED;
        p->runnablethreads = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids){
    //if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  struct kthread *t;
  int found = 0;
  //c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    found = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->pstate != RUNNABLE)
        continue;
    
      for(t = p->kthreads; t < &p->kthreads[NTHREAD]; t++){
        if(t->state == RUNNABLE){
          found = 1;
          break;
        }
      }
      if(!found)
        panic("no runnable thread found in runnable process");


      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->kthread = t;
      switchuvm(p, t);
      t->state = RUNNING;
      p->runnablethreads--;
      if(p->runnablethreads == 0)
        p->pstate = RUNNING;

      swtch(&(c->scheduler), t->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->kthread = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  //struct proc *p = myproc();
  struct kthread *t = mykthread();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(t->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&t->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  mykthread()->state = RUNNABLE;
  mykthread()->owner->pstate = RUNNABLE;
  mykthread()->owner->runnablethreads++;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  //struct proc *p = myproc();
  struct kthread *t = mykthread();
  
  if(t == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  t->kchan = chan;
  t->state = SLEEPING;

  sched();

  // Tidy up.
  t->kchan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  struct kthread *t;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    for(t = p->kthreads; t < &p->kthreads[NTHREAD]; t++){
      if(t->state == SLEEPING && t->kchan == chan){
        t->state = RUNNABLE;
        p->runnablethreads++;
        p->pstate = RUNNABLE;
      }
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;
  struct kthread *t;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->ppid == pid){
      for(t = p->kthreads; t < &p->kthreads[NTHREAD]; t++){
        t->killed = 1;
        if(t->state == SLEEPING){
          t->state = RUNNABLE;
          p->pstate = RUNNABLE;
          p->runnablethreads++;
        }
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

int kthread_create(void (*start_func)(), void* stack){
  struct proc *curproc = myproc();
  struct kthread *t;
  int found = 0;

  acquire(&ptable.lock);
  for(t = curproc->kthreads; t < &curproc->kthreads[NTHREAD]; t++){
    if(t->state == UNUSED){
      found = 1;
      break;
    }
  }
  if(!found){
    release(&ptable.lock);
    return -1;
  }

  // Allocate kernel stack.
  if((t->kstack = kalloc()) == 0){
    t->state = UNUSED;
    release(&ptable.lock);
    return -1;
  }
  t->kpid = nextpid++;
  t->owner = curproc;
  t->killed = 0;
  t->name[0] = 0;
  t->state = EMBRYO;
  
  //our code***
  char *sp = t->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *t->tf;
  t->tf = (struct trapframe*)sp;
  memset(t->tf, 0, sizeof(*t->tf));
  *(t->tf) = *(mykthread()->tf);

  //setup trapframe
  t->tf->eip = (uint)start_func;
  t->tf->ebp = (uint)stack;
  t->tf->esp = (uint)stack;
  t->tf->eax = 0;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *t->context;
  t->context = (struct context*)sp;
  memset(t->context, 0, sizeof *t->context);
  t->context->eip = (uint)forkret;

  t->state = RUNNABLE;
  t->owner->pstate = RUNNABLE;
  t->owner->runnablethreads++;

  release(&ptable.lock);
  return t->kpid;
  //end of our code***
}
int kthread_id(){
  return mykthread()->kpid;
}
void kthread_exit(){
  struct proc *curproc = myproc();
  struct kthread *t;
  struct kthread *curthread = mykthread();
  int last = 1;

  acquire(&ptable.lock); 
  for(t = curproc->kthreads; t < &curproc->kthreads[NTHREAD]; t++){
    if(t->kpid == curthread->kpid || t->state == UNUSED || t->state == ZOMBIE) {
      continue;
    }
    last = 0;
  }

  if(!last) {
    curthread->state = ZOMBIE;
    wakeup1(curthread);
    
    sched();
    panic("at exit thread, not supposed to be here!\n");
    release(&ptable.lock);

    return;
  }

  else {
    release(&ptable.lock);
    exitproc();
  }
}
int kthread_join(int thread_id){
  struct proc *curproc = myproc();
  struct kthread *t;

  if(mykthread()->kpid == thread_id)
    panic("join on calling thread\n");
    
  acquire(&ptable.lock);
  if(mykthread()->killed){
    release(&ptable.lock);
    kthread_exit();
  }
  for(t = curproc->kthreads; t < &curproc->kthreads[NTHREAD]; t++){
    if(t->kpid != thread_id)
      continue;
    if(t->state != ZOMBIE){
      sleep(t, &ptable.lock);
    }

    if(t->state != ZOMBIE){
      release(&ptable.lock);
      return kthread_join(thread_id);
    }

    kfree(t->kstack);
    t->kpid = 0;
    t->owner = 0;
    t->context = 0;
    t->tf = 0;
    t->killed = 0;
    t->name[0] = 0;
    t->state = UNUSED;
    release(&ptable.lock);
    return 0;
    
  }
  release(&ptable.lock);
  return -1;
}

int kthread_mutex_alloc(){
  struct kmutex* m;
  acquire(&mutex.lock);
  for(m = mutex.mutex; m < &mutex.mutex[MAX_MUTEXES]; m++){
    if(m->id == 0){
      m->id = nextmutex++;
      m->locked = 0;
      m->creator = myproc()->ppid;
      release(&mutex.lock);
      return m->id;
    }
  }
  //no free mutex is available
  release(&mutex.lock);
  return -1;
}

//dealloc all process mutexts even if mutex is locked
void deallocallprocessmutexs() {
  struct kmutex *m;
  acquire(&mutex.lock);
  for(m = mutex.mutex; m < &mutex.mutex[MAX_MUTEXES]; m++){
    if(m->creator == myproc()->ppid){
      m->id = 0;
      m->locked = 0;
      m->lockingkthread = 0;
      m->creator = 0;
    }
  }
  release(&mutex.lock);
}

int kthread_mutex_dealloc(int mutex_id){
  struct kmutex* m;
  acquire(&mutex.lock);
  for(m = mutex.mutex; m < &mutex.mutex[MAX_MUTEXES]; m++){
    if(m->id == mutex_id && !(m->locked)){
      m->id = 0;
      m->lockingkthread = 0;
      m->creator = 0;
      release(&mutex.lock);
      return 0;
    }
  }
  //no such mutex id or mutex is locked
  release(&mutex.lock);
  return -1;
}

int kthread_mutex_lock(int mutex_id){
  struct kmutex* m;
  struct kthread* curthread = mykthread();
entry:
  acquire(&mutex.lock);
  for(m = mutex.mutex; m < &mutex.mutex[MAX_MUTEXES]; m++){
    if(m->id == mutex_id){
      if(!m->locked){
        //lock the mutex
        m->locked = 1;
        m->lockingkthread = curthread->kpid;
        release(&mutex.lock);
        return 0;
      }
      else{
        if(m->lockingkthread == curthread->kpid){
          release(&mutex.lock);
          return -1;
        }
        //go into BLOCKED state
        curthread->state = SLEEPING;
        curthread->kchan = m;
        release(&mutex.lock);
        acquire(&ptable.lock);
        sched();
        //woken up from waiting for mutex
        release(&ptable.lock);
        curthread->kchan = 0;
        goto entry;
      }
    }
  }
  //no such mutex id or mutex is locked
  release(&mutex.lock);
  return -1;
}

int kthread_mutex_unlock(int mutex_id){
  struct kmutex* m;
  struct kthread* curthread = mykthread();
  acquire(&mutex.lock);
  for(m = mutex.mutex; m < &mutex.mutex[MAX_MUTEXES]; m++){
    if(m->id == mutex_id && m->lockingkthread == curthread->kpid && m->locked){
      //unlock the mutex
      m->locked = 0;
      m->lockingkthread = 0;
      //wakeup sleeping threads
      wakeup(m);
      release(&mutex.lock);
      return 0;
    }
  }
  release(&mutex.lock);
  return -1;
}


//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pstate == UNUSED)
      continue;
    if(p->pstate >= 0 && p->pstate < NELEM(states) && states[p->pstate])
      state = states[p->pstate];
    else
      state = "???";
    cprintf("%d %s %s", p->ppid, state, p->pname);
    if(p->pstate == SLEEPING){
      getcallerpcs((uint*)mykthread()->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
