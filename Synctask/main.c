#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include "sync_op.h"



int do_work(void *data)
{
    int n = *((int*)data);
    int i=1;
    int sum=0;
    for(i=1; i!=n; ++i)
    {
        sum+=i;
    } 

    printf("sum is %d\n", sum);
}

int
main(int argc, char* argv[])
{
    struct syncenv* env = NULL;


    int ret = synctask_init();
    if(ret)
    {
        fprintf(stderr, "synctask_init failed\n");
        exit(1);
    }


    env=syncenv_new(4024,1,15);
    if(env == NULL)
    {
        printf("create env failed\n");
        exit(1);
    }
    int *n = malloc(sizeof(int)); 
   
    int i = 0 ; 
    *n=100000000;
    while(1)
    {
        synctask_new(env, do_work,NULL, (void*)n);
        printf("%d proc:%d \n",i, env->procs);
        ++i;
        if(i%100==0)
            sleep(110);
    }
    exit(0);
}
