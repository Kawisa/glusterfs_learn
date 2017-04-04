#include "sync_op.h"
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_key_t synctask_key;



void syncenv_destroy(struct syncenv* env)
{

}


struct syncenv*
syncenv_new(size_t stacksize, int procmin, int procmax)
{
    struct syncenv* newenv=NULL;
    if (!procmin||procmin<0)
        procmin = SYNCENV_PRO_MIN;
    if (!procmax||procmax>SYNCENV_PRO_MAX)
        procmax = SYNCENV_PRO_MAX; 

    if(procmin>procmax)
        return NULL;
    
    newenv=(struct syncenv*)calloc(1, sizeof(*newenv));
    if (!newenv)
        return NULL;
    
    pthread_mutex_init(&newenv->mutex, NULL);
    pthread_cond_init(&newenv->cond, NULL);    


    INIT_LIST_HEAD(&newenv->runq);

    newenv->stacksize = SYNCENV_DEFAULT_STACKSIZE;
    if (newenv->stacksize)
        newenv->stacksize = stacksize; 

    newenv->procmin = procmin;
    newenv->procmax = procmax;
   
    int i = 0; 
    int ret = 0;
    for ( i=0; i<newenv->procmin; i++){
        newenv->proc[i].env = newenv;
        ret = let_create_thread(&newenv->proc[i].processor, NULL,
                syncenv_processor, &newenv->proc[i]);
        if (ret)
            break;

        newenv->procs++;

    }
    if (ret!=0)
        syncenv_destroy(newenv);
    
   return newenv; 
}


int let_create_thread(pthread_t* thread, pthread_attr_t *attr,
        void *(*start_function)(void*), void* arg)
{
    int ret = pthread_create(thread, attr, start_function, arg);

    return ret;

}

void *syncenv_processor(void *thdata)
{
    struct syncenv* env = NULL;
    struct syncproc* proc = NULL;
    struct synctask *task = NULL;
    
    proc = thdata;
    env = proc->env;
    
    for(;;){
        task = syncenv_task(proc);
        if (!task)
            break;
        synctask_switchto(task);
        syncenv_scale(env);
    }

}


//取任务
struct synctask* syncenv_task(struct syncproc* proc){
    struct syncenv* env;
    struct synctask* task;
    struct timespec sleep_time = {0, };


    int ret = 0;
    env = proc->env;
    
    pthread_mutex_lock(&env->mutex);
    {
        while(list_empty(&env->runq)){
            sleep_time.tv_sec = time(NULL) + SYNCSLEEP_TIME;
            ret = pthread_cond_timedwait(&env->cond,  &env->mutex, &sleep_time);
            
            if(!list_empty(&env->runq))
                break;
            if((ret == ETIMEDOUT)&&(env->procs > env->procmin)){
                task = NULL;
                env->procs--;
                memset(proc, 0, sizeof(*proc));
                goto unlock;
            }
        }
        //runq 不为空，而且该线程抢到了领取任务的权利
        task = list_entry(env->runq.next, struct synctask, all_tasks);
        
        list_del_init(&task->all_tasks);
        env->runcount--;
        task->proc = proc;
    }
unlock:
    pthread_mutex_unlock(&env->mutex);

    return task;

}

void synctask_switchto(struct synctask *task){
    struct syncenv* env = NULL;
    env = task->env;
   

    pthread_setspecific(synctask_key, task); 

    if(swapcontext(&task->proc->sched, &task->ctx)<0)
    {
        printf("swapcontext failed, exit...\n");
        return;
    }
    if(task->state == SYNCTASK_DONE)
    {
        synctask_done(task);
        return;
    } 
    
    fprintf(stderr, "task doesn't done,but return\n");

}

int synctask_new(struct  syncenv* env, synctask_fn_t fn, synctask_cbk_t cbk, void* data)
{
    int ret = -1;
    struct synctask *newtask = NULL;
    newtask = synctask_create(env, fn, cbk, data);
    if(!newtask)
        return -1;

    if (!cbk)
        ret = synctask_join(newtask);
    return ret;

}

struct synctask* synctask_create(struct syncenv* env, synctask_fn_t fn, synctask_cbk_t cbk, void* data)
{
    struct synctask* newtask = NULL;

    newtask = calloc(1,sizeof(*newtask));
    if(!newtask)
        return NULL;

    newtask->data = data;
    newtask->env = env;
    newtask->sync_fn = fn;
    newtask->sync_cbk = cbk;
    INIT_LIST_HEAD(&newtask->all_tasks);

    if(getcontext(&newtask->ctx)<0)
    {
        fprintf(stderr,"getcontext error\n");
        goto err;
    }

    newtask->stack = calloc(1, newtask->env->stacksize);
    if(!newtask->stack)
    {
        fprintf(stderr, "in synctask_create,out of memery\n");
        goto err;
    }

    newtask->ctx.uc_stack.ss_sp = newtask->stack; 
    newtask->ctx.uc_stack.ss_size = newtask->env->stacksize;
    makecontext(&newtask->ctx, (void (*)(void))synctask_wrap, 2, newtask);

    newtask->state = SYNCTASK_INIT;
    newtask->slept = 1; 

    if(!cbk)
    {
        pthread_mutex_init(&newtask->mutex, NULL);
        pthread_cond_init(&newtask->cond, NULL);
    }

    synctask_wake(newtask);

    syncenv_scale(env);
    return newtask;
err:
    if(newtask)
    {
        free(newtask->stack);
        free(newtask);
    }
    return NULL;
}


void syncenv_scale(struct syncenv* env){
    int diff = 0;
    int i = 0;
    int ret = 0;
    int scale = 0;
    pthread_mutex_lock(&env->mutex);
    {
        if(env->procs  > env->runcount)
            goto unlock; 
        scale = env->runcount;
        if(scale > env->procmax)
            scale = env->procmax;
        if(scale > env->procs)
            diff = scale - env->procs;
        while(diff)
        {
            diff--;
            for(;(i<env->procmax);++i)
            {
                if(env->proc[i].processor == 0)
                    break;
            }
            env->proc[i].env = env;
            ret = let_create_thread(&env->proc[i].processor, NULL, syncenv_processor, &env->proc[i]);
            if(ret)
                break;
            env->procs++;
            i++;
        }

    } 
unlock:
    pthread_mutex_unlock(&env->mutex);
}



void synctask_wrap(struct synctask* old_task)
{
    struct synctask* task = NULL;
    task = pthread_getspecific(synctask_key); 
    task->ret = task->sync_fn(task->data);
    if(task->sync_cbk)
        task->sync_cbk(task->ret);
    task->state = SYNCTASK_DONE;
    synctask_yield(task);
}


int synctask_join(struct synctask* task)
{
    int ret = 0;
    pthread_mutex_lock(&task->mutex); 
    {
        while(!task->done)
            pthread_cond_wait(&task->cond, &task->mutex);
    }
    pthread_mutex_unlock(&task->mutex);
    ret = task->ret;
    synctask_destroy(task);
    return ret;
}

void _run(struct synctask* task)
{
    struct syncenv* env = NULL;
    env = task->env;
    list_del_init(&task->all_tasks);
    switch(task->state)
    {
    case SYNCTASK_INIT:
        break; 
    
    }
    list_add_tail(&task->all_tasks, &env->runq);
    env->runcount++;
    task->state = SYNCTASK_RUN;
}




void synctask_wake(struct synctask* newtask)
{   
    struct syncenv* env = NULL;
    env = newtask->env;
    pthread_mutex_lock(&env->mutex);
    {
        newtask->woken = 1;
        if(newtask->slept) 
            _run(newtask);

        pthread_cond_broadcast(&env->cond);

    }
    pthread_mutex_unlock(&env->mutex);
}


void synctask_done(struct synctask*task)
{
    if(task->sync_cbk)
    {
        synctask_destroy(task);
        return;
    }
    pthread_mutex_lock(&task->mutex);
    {
        task->state = SYNCTASK_ZOMBIE;
        task->done = 1;
        pthread_cond_broadcast(&task->cond);
    }
    pthread_mutex_unlock(&task->mutex);
}


//task was done, come back
void synctask_yield(struct synctask*task)
{
    if(swapcontext(&task->ctx, &task->proc->sched)<0)
    {
        fprintf(stderr, "swapcontext failed, error:%s\n", strerror(errno));
    }
    return;
    
}

void synctask_destroy(struct synctask* task)
{
    free(task->stack);
    if(task->sync_cbk == NULL)
    {
        pthread_mutex_destroy(&task->mutex);
        pthread_cond_destroy(&task->cond);
    }
    free(task);

}


int synctask_init()
{
    int ret = 0;
    ret = pthread_key_create(&synctask_key, NULL);

    return ret;

}







