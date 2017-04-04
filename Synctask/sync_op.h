#include <pthread.h>
#include <ucontext.h>
#include <signal.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

//about list_head  ------------------------------------------------------------
//glusterfs里的双向链表但是是无类型的,内核里也有用这种
//这里只实现几个用的到的操作
struct list_head{
    struct list_head *next;
    struct list_head *prev;
};

#define INIT_LIST_HEAD(head) do {    \
    (head)->next  = (head)->prev = head; \
}while(0)

static inline int 
list_empty(struct list_head* head)
{
    return (head->next == head);
}

static inline void
list_del_init(struct list_head* old)
{
    old->prev->next = old->next;
    old->next->prev = old->prev;
    
    old->next = old;
    old->prev = old;
}

static inline void
list_add_tail(struct list_head* new, struct list_head* head)
{
    new->next = head;
    new->prev = head->prev;
    
    new->prev->next = new;
    new->next->prev = new;
} 


#define list_entry(ptr, type, member) \
    ((type*)((char*)(ptr) - (unsigned long)(&((type*)0)->member)))




#define SYNCENV_PRO_MAX 5 
#define SYNCENV_PRO_MIN 2
#define SYNCENV_DEFAULT_STACKSIZE 1024*1024*4
#define SYNCSLEEP_TIME 10


typedef enum{
    SYNCTASK_INIT = 0,
    SYNCTASK_RUN,
    SYNCTASK_DONE,
    SYNCTASK_ZOMBIE
}synctask_state_t;

typedef int (*synctask_fn_t)(void* data);
typedef int (*synctask_cbk_t)(int ret);



struct syncproc{
    pthread_t processor;
    ucontext_t sched;
    struct syncenv* env;
};

struct syncenv{
    struct syncproc proc[SYNCENV_PRO_MAX];
    int procs;
    struct list_head runq;
    int  runcount;
    int procmin;
    int procmax;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    size_t stacksize;
};



struct synctask{
    struct list_head all_tasks;
    struct syncenv* env;
    synctask_fn_t sync_fn;
    synctask_cbk_t sync_cbk;
    int ret;
    void * data;
    
    ucontext_t ctx;
    struct syncproc* proc;


    void *stack;
    
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    
    
    synctask_state_t state;
    int slept;
    int woken;
    int done;
};


struct syncenv* syncenv_new(size_t stacksize, int promin, int promax);
void syncenv_scale(struct syncenv* env);
void* syncenv_processor(void *thdata);


int synctask_new(struct syncenv*, synctask_fn_t, synctask_cbk_t, void*);
struct synctask* synctask_create(struct syncenv*, synctask_fn_t, synctask_cbk_t, void*);
int synctask_join(struct synctask*);
void synctask_wrap(struct synctask*);
void synctask_wake(struct synctask* newtask);
void synctask_done(struct synctask* task);
void synctask_yield(struct synctask* task);
void synctask_destroy(struct synctask* task);
int synctask_init();

struct synctask* syncenv_task(struct syncproc* proc);
void synctask_switchto(struct synctask* task);





int let_create_thread(pthread_t* thread, pthread_attr_t *attr,
                        void *(*start_function)(void*), void* arg);




struct synctask* syncenv_task(struct syncproc* proc);
void synctask_switchto(struct synctask* task);





int let_create_thread(pthread_t* thread, pthread_attr_t *attr,
                        void *(*start_function)(void*), void* arg);










