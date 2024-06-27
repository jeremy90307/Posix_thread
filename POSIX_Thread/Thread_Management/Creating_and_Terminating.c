#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define NUM_THREAD 4

void *print(void *thread_id)
{
    long tid;
    tid = (long)thread_id;
    printf("Hello World!, By thread #%ld\n", tid);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    /* Create NUM_THREADS threads */ 
    pthread_t threads[NUM_THREAD];
    /*Receive the return value of the pthread_create function*/ 
    int rc;
    long t;
    for(t = 0; t < NUM_THREAD; t++){
        printf("Create thread %ld\n",t);
        rc = pthread_create(&threads[t], NULL, print, (void *)t);
        /* If pthread_create successfully creates a thread, the return value rc will be 0 */ 
        if (rc){
            printf("ERROR\n");
            exit(-1);
        }
    }
    /* Prevent the whole process from terminating due to main() ending first */ 
    pthread_exit(NULL);
}