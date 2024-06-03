#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_THREADS 3

void *print(void *thread_id) {
    long tid = (long) thread_id;
    printf("Hello World!, By thread #%ld\n", tid);
    pthread_exit((void *) tid);
}

int main(int argc, char *argv[]) {
    pthread_t threads[NUM_THREADS];
    /* Declare a pthread attribute variable of the pthread_attr_t data type */ 
    pthread_attr_t attr;
    int rc;
    long t;
    void *status;

    /* Initialize and set thread detached attribute */
    /* PTHREAD_CREATE_JOINABLE is joinable */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    for (t = 0; t < NUM_THREADS; t++) {
        printf("Creating thread %ld\n", t);
        rc = pthread_create(&threads[t], &attr, print, (void *)t);
        if (rc) {
            printf("ERROR\n");
            exit(-1);
        }
    }

    /* Free attribute and wait for the other threads */
    pthread_attr_destroy(&attr);

    for (t = 0; t < NUM_THREADS; t++) {
        rc = pthread_join(threads[t], &status);
        if (rc) {
            printf("ERROR\n");
            exit(-1);
        }
        printf("Completed join with thread %ld having status %ld\n", t, (long)status);
    }

    printf("Main thread completed.\n");
    pthread_exit(NULL);
}
