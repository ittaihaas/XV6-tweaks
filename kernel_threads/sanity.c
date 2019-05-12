#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"
#include "tournament_tree.h"
#include "kthread.h"

int stdout = 1;
int globalinteger, globalmutexid;

//globals for advance tree test
  trnmnt_tree *tree;
  struct kthread *threads[16];
  int ids[16];
  int holdexecution = 1;
//

void* allocstack(){
  void* stack = malloc(MAX_STACK_SIZE);
  stack = stack + MAX_STACK_SIZE;
  return stack;
}

void func0(){
  kthread_exit();
}

void func1(){
  sleep(50);
  int i = 3;
  int ii = 33;
  int iii = 333;
  globalinteger = globalinteger * globalinteger;
  globalinteger += (i + ii + iii);
  globalinteger -= (i + ii + iii);

  kthread_exit();
}

void func2(){
  sleep(1000);
  kthread_exit();
}

void func3(){
  int i, found;
  int id = 0;
  for(i = 0; i < 16; i++){
    if(ids[i] == kthread_id()){
      found = 1;
      id = i;
    }
  }
  if(!found){
    printf(1, "error with func3 id\n");
    kthread_exit();
  }
  while(holdexecution){}
  sleep((id * 991 * 223) % 500);
  if(trnmnt_tree_acquire(tree, id) == -1){
    printf(1, "error with tree acquire\n");
    kthread_exit();
  }

  printf(1, "this text from ID: %d should be readable\n", id);

  if(trnmnt_tree_release(tree, id) == -1){
    printf(1, "error with tree release\n");
    kthread_exit();
  }
  kthread_exit();
}

int
kthreadcreate(){
  void *dellocs[16];
  int i, thread_id = 0;
  void (*fun_ptr)() = &func0;
  void *stack;
  for (i = 0; i < 15; i++){
    stack = allocstack();
    dellocs[i] = stack;
    thread_id = kthread_create(fun_ptr, stack);
    sleep(10);
    if(thread_id < 0){
      while(i > -1)
        free(dellocs[i--]);
      return 0;
    }
  }
  i--;
  stack = allocstack();
  thread_id = kthread_create(fun_ptr, stack);
  if(thread_id != -1){
    while(i > -1)
      free(dellocs[i--]);
    return 0;
  }
  sleep(10);
  while(i > -1)
    free(dellocs[i--]);
  return 1;
}

int kthreadjoin(){
  int thread_id;
  void (*fun_ptr)() = &func1;
  void *stack = allocstack();
  globalinteger = 2;
  thread_id = kthread_create(fun_ptr, stack);
  kthread_join(thread_id);
  free(stack);
  globalinteger = globalinteger + globalinteger;
  if(globalinteger == 8)
    return 1;
  if(globalinteger == 16)
    return 0;
  return 0;
}

void aqumutex(){
  kthread_mutex_lock(globalmutexid);
  globalinteger = 2;
  kthread_mutex_unlock(globalmutexid);
  kthread_exit();
}

void releasemutex(){
  if(kthread_mutex_unlock(globalmutexid) != -1)
    globalinteger = 2;
  kthread_exit();
}

int basicmutex(){
  int threadid;
  void (*fun_ptr)() = &aqumutex;
  globalinteger = 1;
  globalmutexid = kthread_mutex_alloc();
  if(kthread_mutex_lock(globalmutexid) == -1)
    return 0;
  void* stack = allocstack();
  threadid = kthread_create(fun_ptr, stack);
  sleep(100);
  if(globalinteger != 1)
    return 0;
  kthread_mutex_unlock(globalmutexid);
  kthread_join(threadid);
  free(stack);
  if(globalinteger != 2)
    return 0;
  if(kthread_mutex_dealloc(globalmutexid) == -1)
    return 0;
  return 1;
}

int moremutex(){
  int mutexids[MAX_MUTEXES];
  int i;
  void (*fun_ptr)() = &releasemutex;
  for(i = 0; i < MAX_MUTEXES; i++){
    mutexids[i] = kthread_mutex_alloc();
    if(mutexids[i] == -1)
      return 0;
  }
  if(kthread_mutex_alloc() != -1)
    return 0;
  if(kthread_mutex_lock(-1) != -1)
    return 0;
  globalmutexid = mutexids[0];
  globalinteger = 0;
  if(kthread_mutex_lock(globalmutexid) == -1)
    return 0;
  i = kthread_create(fun_ptr, allocstack());
  if(i < 0)
    return 0;
  kthread_join(i);
  if(globalinteger == 2)
    return 0;
  if(kthread_mutex_unlock(-1) != -1)
    return 0;
  if(kthread_mutex_dealloc(globalmutexid) != -1)
    return 0;
  if(kthread_mutex_unlock(globalmutexid) == -1)
    return 0;
  if(kthread_mutex_dealloc(globalmutexid) == -1)
    return 0;
  for(i = 0; i < MAX_MUTEXES; i++){
    kthread_mutex_unlock(mutexids[i]);
    kthread_mutex_dealloc(mutexids[i]);
  }
  return 1;
}

int basictree(){
  trnmnt_tree* tree = trnmnt_tree_alloc(2);
  if(tree == 0)
    return 0;
  if(trnmnt_tree_acquire(tree, 1) == -1){
    printf(1, "failed accuire\n");
    return 0;
  }
  if(trnmnt_tree_release(tree, 1) == -1){
    printf(1, "failed release\n");
    return 0;
  }
  if(trnmnt_tree_dealloc(tree) == -1){
    printf(1, "failed dealloc\n");
    return 0;
  }
  return 1;
}

int advancetree(){
  int i;
  void *stacks[16];
  void (*fun_ptr)() = &func3;

  tree = trnmnt_tree_alloc(4);
  if(tree == 0)
    return 0;

  for(i = 0; i < 16; i++){
    stacks[i] = allocstack();
    ids[i] = kthread_create(fun_ptr, stacks[i]);
    if(ids[i] == 0){
      printf(1, "error creating thread\n");
      exit();
    }
  }
  holdexecution = 0;
  for(i = 0; i < 16; i++){
    kthread_join(ids[i]);
    free(stacks[i]);
  }
  if(trnmnt_tree_dealloc(tree) == -1)
    return 0;
  return 1;
}


int
main(int argc, char *argv[])
{
  int pid, i;
  int numberoftests = 6;
  int (*test[])() = {&kthreadcreate, &kthreadjoin, &basicmutex, &moremutex, &basictree, &advancetree};
  char* names[] = {"kthread create", "kthread join", "basic mutex", "more mutex", "basic tree", "adcance tree"};

  printf(1, "sanity starting, main pid: %d\n", kthread_id());
  for (i = 0; i < numberoftests; i++){
    pid = fork();
    if(pid){
      wait();
    }
    else{
      if(test[i]()){
        printf(1, "%s passed\n", names[i]);
        exit();
      }
      else{
      printf(1, "%s failed\n", names[i]);
      exit();
      }
    }
  }

  printf(1, "sanity done\n");
  exit(); 
  return -1;
}
