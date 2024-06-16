#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#define TRY(f)  \
    do {    \
        int __r;    \
        if((__r = (f != 0))){   \
            fprintf(stderr, "Run function %s = %d (errno: %d, %s)\n", #f, __r, errno, strerror(errno));  \
            return __r; \
        }   \
    } while (0)

static int pthread_create_with_prio(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void *), void *arg, int prio)
{
    struct sched_param param;
    param.sched_priority = prio;

    TRY(pthread_attr_setschedparam(attr, &param));
    TRY(pthread_create(thread, attr, start_routine, arg));

    return 0;
}

static void *start_routine(void *arg)
{
    int i, j;
    while(1)
    {
        fprintf(stderr, "%c ", *(char *)arg);
        for(i=0; i<100000; i++)
			for(j=0; j<10000; j++);
    }
    pthread_exit(NULL);
}

int main(void)
{
    pthread_t tid1, tid2, tid3;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    TRY(pthread_attr_setschedpolicy(&attr, SCHED_FIFO));
    TRY(pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED));
    
    printf("Main process PID: %d\n", getpid());

    TRY(pthread_create_with_prio(&tid1, &attr, start_routine, (void *)"1", 1));
    TRY(pthread_create_with_prio(&tid2, &attr, start_routine, (void *)"2", 1));
    TRY(pthread_create(&tid3, NULL, start_routine, (void *)"3"));
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);
    pthread_join(tid3, NULL);

    pthread_attr_destroy(&attr);

    return 0;
}
