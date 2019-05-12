#include "tournament_tree.h"
#include "kthread.h"
#include "types.h"
#include "stat.h"
#include "user.h"


void resetactivethreads(trnmnt_tree *tree){
  int i;
  for(i = 0; i < 2 << (tree->depth - 1); i++)
    tree->activethreads[i] = 0;
}

void resetbaselevel(trnmnt_tree *tree){
  int i;
  for(i = 0; i < MAX_MUTEXES; i++)
    tree->baselevel[i] = 0;
}

int allocnodes(trnmnt_tree *tree, struct node *n, int depth){
  if(depth){
    n->leftchild = (struct node*)malloc(sizeof(struct node));
    n->rightchild = (struct node*)malloc(sizeof(struct node));
    if(n->leftchild == 0 || n->rightchild == 0)
      return 0;

    n->leftchild->mutexid = kthread_mutex_alloc();
    n->rightchild->mutexid = kthread_mutex_alloc();
    if(n->leftchild->mutexid < 0 || n->rightchild->mutexid < 0)
      return 0;

    n->leftchild->parent = n;
    n->rightchild->parent = n;
    
    if(depth -1 == 0){
      //base level nodes
      n->leftchild->nodeid = tree->nodesindex++;
      n->rightchild->nodeid = tree->nodesindex++;
      tree->baselevel[n->leftchild->nodeid] = n->leftchild;
      tree->baselevel[n->rightchild->nodeid] = n->rightchild;
    }
    return (allocnodes(tree, n->leftchild, depth-1) && allocnodes(tree, n->rightchild, depth-1));
  }
  return 1;
}

int dellocnodes(struct node *n){
  int res0 = 0;
  int res1 = 0;
  int res2 = 0;
  if(n){
    res0 = dellocnodes(n->leftchild);
    res1 = dellocnodes(n->rightchild);
    res2 = kthread_mutex_dealloc(n->mutexid);
    n->nodeid = 0;
    n->mutexid = 0;
    n->aquiredthreadid = 0;
    n->parent = 0;
    n->leftchild = 0;
    n->rightchild = 0;
    free(n);
    if(res0 == -1 || res1 == -1 || res2 == -1)
      return -1;
    return 0;
  }
return 0;
}

trnmnt_tree* trnmnt_tree_alloc(int depth){
  struct trnmnt_tree *tree; 
  tree = malloc(sizeof(struct trnmnt_tree));
  if(tree == 0)
    return 0;
  tree->root = malloc(sizeof(struct node));
  if(tree->root == 0)
    return 0;
  tree->root->mutexid = kthread_mutex_alloc();
  if(tree->root->mutexid < 0)
    return 0;
  tree->threadsmutex = kthread_mutex_alloc();
  if(tree->threadsmutex < 0)
    return 0;
  tree->activethreads = malloc(sizeof(int) * (2 << (depth-1)));
  if(tree->activethreads == 0)
    return 0;

  tree->depth = depth;
  tree->nodesindex = 0;
  resetactivethreads(tree);
  resetbaselevel(tree);
  if(depth == 1){
    //root only tree
    tree->baselevel[0] = tree->root;
    tree->root->nodeid = tree->nodesindex++;
    return tree;
  }
  else{
    if(allocnodes(tree, tree->root, depth-1) == 0)
      return 0;
    return tree;
  }
}

int trnmnt_tree_dealloc(trnmnt_tree* tree){
  int i, res;
  //check for wating threads in the tree
  kthread_mutex_lock(tree->threadsmutex);
  for(i = 0; i < 2 << (tree->depth - 1); i++){
    if(tree->activethreads[i] == 1){
      kthread_mutex_unlock(tree->threadsmutex);
      return -1;
    }
  }
  res = dellocnodes(tree->root);
  kthread_mutex_unlock(tree->threadsmutex);

  kthread_mutex_dealloc(tree->threadsmutex);
  free(tree->activethreads);
  free(tree);
  return res;
}

int aquirelevelup(struct node *n, int ID){
  if(kthread_mutex_lock(n->mutexid) == -1)
    return -1;
  n->aquiredthreadid = ID;
  if(n->parent == 0)
    return 1;
  return aquirelevelup(n->parent, ID);
}

int trnmnt_tree_acquire(trnmnt_tree* tree,int ID){
  kthread_mutex_lock(tree->threadsmutex);
  tree->activethreads[ID] = 1;
  kthread_mutex_unlock(tree->threadsmutex);
  struct node *initialnode = tree->baselevel[ID / 2];
  if(initialnode == 0)
    return -1;
  if(kthread_mutex_lock(initialnode->mutexid) == -1)
    return -1;
  initialnode->aquiredthreadid = ID;
  if(initialnode->parent == 0)
    return 1;
  //not root node, continue climbing
  return aquirelevelup(initialnode->parent, ID);
}

int realeseleveldown(struct node *n, int ID){
  if(kthread_mutex_unlock(n->mutexid) == -1)
    return -1;
  if(n->leftchild && n->leftchild->aquiredthreadid == ID){
    return realeseleveldown(n->leftchild, ID);
  }
  if(n->rightchild && n->rightchild->aquiredthreadid == ID){
    return realeseleveldown(n->rightchild, ID);
  }
  //no children, release is done
  return 1;
}

int trnmnt_tree_release(trnmnt_tree* tree, int ID){
  struct node *root = tree->root;
  if(root != 0 && root->aquiredthreadid != ID)
    return -1;
  if(root->leftchild && root->leftchild->aquiredthreadid == ID){
    if(kthread_mutex_unlock(root->mutexid) == -1)
      return -1;
    if(realeseleveldown(root->leftchild, ID) == -1)
      return -1;
    kthread_mutex_lock(tree->threadsmutex);
    tree->activethreads[ID] = 0;
    kthread_mutex_unlock(tree->threadsmutex);
    return 0;
  }
  if(root->rightchild && root->rightchild->aquiredthreadid == ID){
    if(kthread_mutex_unlock(root->mutexid) == -1)
      return -1;
    if(realeseleveldown(root->rightchild, ID) == -1)
      return -1;
    kthread_mutex_lock(tree->threadsmutex);
    tree->activethreads[ID] = 0;
    kthread_mutex_unlock(tree->threadsmutex);
    return 0;
  }
  //no children
  if(kthread_mutex_unlock(root->mutexid) == -1)
    return -1;
  kthread_mutex_lock(tree->threadsmutex);
  tree->activethreads[ID] = 0;
  kthread_mutex_unlock(tree->threadsmutex);
  return 0;
}
