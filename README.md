# XV6-tweaks

# Kernel_threads:
Implementation of kernael managed threads, the system now runs threads instead of processes.
Mutex and a tournament tree for mutual exclusion is also implemented, mutex as system calls and the tournament tree as user library.

Threads:

kthread_create(void (∗startfunc)(), void ∗stack)

kthread_id()

kthread_exit()

kthread_join(int thread_id)

Mutex:

int kthread_mutex_alloc()

int kthread_mutex_dealloc(int mutex_id)

int kthread_mutex_lock(int mutex_id)

int kthread_mutex_unlock(int mutex_id)

Tournament tree:

trnmnt_tree* trnmnt_tree_alloc(int depth)

int trnmnt_tree_dealloc(trnmnt_tree* tree)

int trnmnt_tree_acquire(trnmnt_tree* tree,int ID)

int trnmnt_tree_release(trnmnt_tree* tree,int ID)
