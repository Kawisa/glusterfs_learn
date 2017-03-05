###Synctask
 glusterfs里包含一个处理任务请求的线程池（通过ucontext库实现，另一种说法是叫协程）。  

实现文件：   
Syncop.c 主要包括几个重要的函数:  

* syncenv* syncenv_new(size_t stacksize,int procmin, int promax)   		
	
	负责整个线程池的初始化, 第一个参数 stacksize 规定了每个线程的stack的大小， 第二个和第三个参数分别规定了 线程池最少和最多 包含的线程数。 通常glusterfs进程在初始化的时候就会调用此函数，之后会新建一些线程(syncenv_processor)来等待工作到来
	返回一个syncenv的数据结构，维护着整个线程池的状态 

 ```
	struct syncenv {        	struct syncproc     proc[SYNCENV_PROC_MAX];        	int                 procs;        	struct list_head    runq;        	int                 runcount;        	struct list_head    waitq;        	int                 waitcount;			int                 procmin;			int                 procmax;       		pthread_mutex_t     mutex;        	pthread_cond_t      cond;        	size_t              stacksize;		}; ```* syncenv_processor(syncenv->proc[i])  
	一个工作线程，主要信息存放在下面的数据结构之内。
	这种线程就干三件事：  
	1.task=syncenv_task 尝试领取任务，如果无任务，且当前线程池里线程数量多于procmin，则会销毁一些线程   
	2.synctask_switchto(task) 有任务则去执行  
	3.syncenv_scale(env) 任务有积压了，新建工作线程
 
  ```
	struct syncproc {          pthread_t           processor; //线程号        ucontext_t          sched; //存放上下文        struct syncenv     *env;	//对应的线程池env        struct synctask    *current;	//当前对应的task	};
 ```  
* sysncenv_task(proc) 参数，自身的proc结构   
  尝试领取任务，env-runq任务队列通过互斥量和条件变量保护，线程首先对互斥量加锁，随后检查条件,所有线程条件变化领取任务。
  	1.prthread_mutex_lock(&env->mutex）
  	2. while(empty(env->runq)) pthread_cond_timedwait(&env->cond,&env->mutex,time)
 	)
* synctask_switchto(task)
	switch过去真正执行任务。
	1. synctask_set(task) //将task和线程私有数据key绑定
	2. swapcontext(&task->proc->sched, &task->ctx) 
		//跳转到task->proc->sched 保存的上下文中，并且将当前上下文保存在task->ctx之中  
		(swapcontext可能回执行失败哦，)
	3. 之后任务执行完还会切回这个上下文   
	
   <br/>
下面就来看创建任务的函数：
* syncstack_new(env,fn,cbk,frame,opaque)  
     newtask=synctask_create(env,fn,cbk,frame,opaque) //创建任务  
     if (!cbk)
     	ret=synctask_join(newtask) //没有cbk就只能等待结束了
* synctask_create(env,fn,cbk,frame,opaque)  
   1.synctask* newtask=new(),初始化值  
   2.getcontext(&newtask->ctx) //生成一个上下文ucontext_t  
   3.newtask->stack =CALLOC(1,env->stackzie) //分配栈空间  
   4.newtask->ctx.uc_stack.ss_sp = newtask->stack  
     newtask->ctx.uc_stack.ss_size = stackze (必须)  
   5.makecontext(&newtask->ctx, (void(*)(void))synctask_wrap),2,newtask);  
   	 调整上面保存的上下文,将上下文的入口指向synctask_wrap  
     newtask->state=SYNCTASK_INIT  
     newtask->slept=1  
     if(!cbk)  
    	init(mutex,cond)  
     	->done=0  
   6.synctask_wake(task)  
   	  newtask->woken=1  
   	  if(task->slept)  
   	  	__run(task)  
   	  	   task->state=SYNCTASK_RUN
     	  	   add_tail(&task->all_tasks,&env->runq) //加到执行队列    
* synctask_wrap(old_task)  
	1.task=synctask_get() //通过线程的私有数据key来获取，    
	2.task->ret = task->syncfn(task->opaque)// 真正的执行  
	3.if (task->syncbk)  
		task->synccbk(task->ret, task->frame,task->opaque)  
	4.task->state=SYNCTASK_DONE  
	  	synctask_yield(task)    
	  
* synctask_yield(task)  
   swapcontext(&task->ctx,&task->proc->sched)//切回工作线程的上下文了  
* synctask_join(task)  
   lock(&task->mutex)
   pthread_cond_wait(&task->cond,&task->mutex)
   unlock
   synctask_destroy  
   
   这是synctask大概的一个框架，当然还有一些glusterfs一些相关的操作没有涉及。后面我会尝试仿照这个框架写一个demo程序来实践一下。