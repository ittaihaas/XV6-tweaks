#define MAX_MUTEXES 64

typedef struct node {
    int nodeid;
    int mutexid;
    int aquiredthreadid;
    struct node *parent;
    struct node *leftchild;
    struct node *rightchild;
} node;

typedef struct trnmnt_tree{
  int depth;
  struct node *root;
  struct node *baselevel[MAX_MUTEXES];
  int *activethreads;
  int threadsmutex;
  int nodesindex;
} trnmnt_tree;
