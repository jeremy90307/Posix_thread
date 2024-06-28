#define _GNU_SOURCE
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "list.h"

static int preempt_count = 0;
static void preempt_disable(void)
{
    preempt_count++;
}
static void preempt_enable(void)
{
    preempt_count--;
}

static void local_irq_save(sigset_t *sig_set)
{
    sigset_t block_set;
    sigfillset(&block_set);
    sigdelset(&block_set, SIGINT);
    sigprocmask(SIG_BLOCK, &block_set, sig_set);
}

static void local_irq_restore(sigset_t *sig_set)
{
    sigprocmask(SIG_SETMASK, sig_set, NULL);
}

#define task_printf(...)     \
    ({                       \
        preempt_disable();   \
        printf(__VA_ARGS__); \
        preempt_enable();    \
    })

typedef void(task_callback_t)(void *arg);

struct task_struct {
    struct list_head list;
    ucontext_t context;
    void *stack;
    task_callback_t *callback;
    void *arg;
    bool reap_self;
};

static struct task_struct *task_current, task_main;
static LIST_HEAD(task_reap);

static void task_init(void)
{
    INIT_LIST_HEAD(&task_main.list);
    task_current = &task_main;
}

static struct task_struct *task_alloc(task_callback_t *func, void *arg)
{
    struct task_struct *task = calloc(1, sizeof(*task));
    task->stack = calloc(1, 1 << 20);
    task->callback = func;
    task->arg = arg;
    return task;
}

static void task_destroy(struct task_struct *task)
{
    list_del(&task->list);
    free(task->stack);
    free(task);
}

static void task_switch_to(struct task_struct *from, struct task_struct *to)
{
    task_current = to;
    swapcontext(&from->context, &to->context);
}

static void schedule(void)
{
    sigset_t set;
    local_irq_save(&set);

    struct task_struct *next_task =
        list_first_entry(&task_current->list, struct task_struct, list);
    if (next_task) {
        if (task_current->reap_self)
            list_move(&task_current->list, &task_reap);
        task_switch_to(task_current, next_task);
    }

    struct task_struct *task, *tmp;
    list_for_each_entry_safe (task, tmp, &task_reap, list) /* clean reaps */
        task_destroy(task);

    local_irq_restore(&set);
}

union task_ptr {
    void *p;
    int i[2];
};

static void local_irq_restore_trampoline(struct task_struct *task)
{
    sigdelset(&task->context.uc_sigmask, SIGALRM);
    local_irq_restore(&task->context.uc_sigmask);
}

__attribute__((noreturn)) static void task_trampoline(int i0, int i1)
{
    union task_ptr ptr = {.i = {i0, i1}};
    struct task_struct *task = ptr.p;

    local_irq_restore_trampoline(task);
    task->callback(task->arg);
    task->reap_self = true;
    schedule();

    __builtin_unreachable(); /* shall not reach here */
}

static void task_add(task_callback_t *func, void *param)
{
    struct task_struct *task = task_alloc(func, param);
    if (getcontext(&task->context) == -1)
        abort();

    task->context.uc_stack.ss_sp = task->stack;
    task->context.uc_stack.ss_size = 1 << 20;
    task->context.uc_stack.ss_flags = 0;
    task->context.uc_link = NULL;

    union task_ptr ptr = {.p = task};
    makecontext(&task->context, (void (*)(void)) task_trampoline, 2, ptr.i[0],
                ptr.i[1]);

    sigaddset(&task->context.uc_sigmask, SIGALRM);

    preempt_disable();
    list_add_tail(&task->list, &task_main.list);
    preempt_enable();
}

static void timer_handler(int signo, siginfo_t *info, ucontext_t *ctx)
{
    if (preempt_count)
        return;
    schedule();
}

static void timer_init(void)
{
    struct sigaction sa = {
        .sa_handler = (void (*)(int)) timer_handler,
        .sa_flags = SA_SIGINFO,
    };
    sigfillset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
}

static void timer_create(unsigned int usecs)
{
    ualarm(usecs, usecs);
}
static void timer_cancel(void)
{
    ualarm(0, 0);
}

static void timer_wait(void)
{
    sigset_t mask;
    sigprocmask(0, NULL, &mask);
    sigdelset(&mask, SIGALRM);
    sigsuspend(&mask);
}

typedef struct {
    uint32_t value;
    struct list_head list;
} element_t;

static int cmp(void *priv,
               const struct list_head *a,
               const struct list_head *b)
{
    element_t *node_a = list_entry(a, element_t, list);
    element_t *node_b = list_entry(b, element_t, list);
    uint32_t diff = node_a->value ^ node_b->value;
    if(!diff)
        /* *a == *b */
        return 0;
    diff = diff | (diff >> 1);
    diff |= diff >> 2;
    diff |= diff >> 4;
    diff |= diff >> 8;
    diff |= diff >> 16;
    diff ^= diff >> 1;
    return (node_a->value & diff) ? 1 : -1;
}

static inline uint32_t random_shuffle(uint32_t x)
{
    /* by Chris Wellons, see: <https://nullprogram.com/blog/2018/07/31/> */
    x ^= x >> 16;
    x *= 0x7feb352dUL;
    x ^= x >> 15;
    x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}

/* Implement timsort. */

typedef int
    __attribute__((nonnull(2, 3))) (*list_cmp_func_t)(void *,
                                                      const struct list_head *,
                                                      const struct list_head *);

static inline size_t run_size(struct list_head *head)
{
    if (!head)
        return 0;
    if (!head->next)
        return 1;
    return (size_t) (head->next->prev);
}

struct pair {
    struct list_head *head, *next;
};

static size_t stk_size;

__attribute__((nonnull(2, 3, 4))) static struct list_head * merge(void *priv,
                               list_cmp_func_t cmp,
                               struct list_head *a,
                               struct list_head *b)
{
    struct list_head *head;
    struct list_head **tail = &head;

    for (;;) {
        /* if equal, take 'a' -- important for sort stability */
        if (cmp(priv, a, b) <= 0) {
            *tail = a;
            tail = &a->next;
            a = a->next;
            if (!a) {
                *tail = b;
                break;
            }
        } else {
            *tail = b;
            tail = &b->next;
            b = b->next;
            if (!b) {
                *tail = a;
                break;
            }
        }
    }
    return head;
}

static void build_prev_link(struct list_head *head,
                            struct list_head *tail,
                            struct list_head *list)
{
    tail->next = list;
    do {
        list->prev = tail;
        tail = list;
        list = list->next;
    } while (list);

    /* The final links to make a circular doubly-linked list */
    tail->next = head;
    head->prev = tail;
}

__attribute__((nonnull(2, 3, 4, 5))) static void merge_final(void *priv,
                        list_cmp_func_t cmp,
                        struct list_head *head,
                        struct list_head *a,
                        struct list_head *b)
{
    struct list_head *tail = head;

    for (;;) {
        /* if equal, take 'a' -- important for sort stability */
        if (cmp(priv, a, b) <= 0) {
            tail->next = a;
            a->prev = tail;
            tail = a;
            a = a->next;
            if (!a)
                break;
        } else {
            tail->next = b;
            b->prev = tail;
            tail = b;
            b = b->next;
            if (!b) {
                b = a;
                break;
            }
        }
    }

    /* Finish linking remainder of list b on to tail */
    build_prev_link(head, tail, b);
}

static struct pair find_run(void *priv,
                            struct list_head *list,
                            list_cmp_func_t cmp)
{
    size_t len = 1;
    struct list_head *next = list->next, *head = list;
    struct pair result;

    if (!next) {
        result.head = head, result.next = next;
        return result;
    }

    if (cmp(priv, list, next) > 0) {
        /* decending run, also reverse the list */
        struct list_head *prev = NULL;
        do {
            len++;
            list->next = prev;
            prev = list;
            list = next;
            next = list->next;
            head = list;
        } while (next && cmp(priv, list, next) > 0);
        list->next = prev;
    } else {
        do {
            len++;
            list = next;
            next = list->next;
        } while (next && cmp(priv, list, next) <= 0);
        list->next = NULL;
    }
    head->prev = NULL;
    head->next->prev = (struct list_head *) len;
    result.head = head, result.next = next;
    return result;
}

static struct list_head *merge_at(void *priv,
                                  list_cmp_func_t cmp,
                                  struct list_head *at)
{
    size_t len = run_size(at) + run_size(at->prev);
    struct list_head *prev = at->prev->prev;
    struct list_head *list = merge(priv, cmp, at->prev, at);
    list->prev = prev;
    list->next->prev = (struct list_head *) len;
    --stk_size;
    return list;
}

static struct list_head *merge_force_collapse(void *priv,
                                              list_cmp_func_t cmp,
                                              struct list_head *tp)
{
    while (stk_size >= 3) {
        if (run_size(tp->prev->prev) < run_size(tp)) {
            tp->prev = merge_at(priv, cmp, tp->prev);
        } else {
            tp = merge_at(priv, cmp, tp);
        }
    }
    return tp;
}

static struct list_head *merge_collapse(void *priv,
                                        list_cmp_func_t cmp,
                                        struct list_head *tp)
{
    int n;
    while ((n = stk_size) >= 2) {
        if ((n >= 3 &&
             run_size(tp->prev->prev) <= run_size(tp->prev) + run_size(tp)) ||
            (n >= 4 && run_size(tp->prev->prev->prev) <=
                           run_size(tp->prev->prev) + run_size(tp->prev))) {
            if (run_size(tp->prev->prev) < run_size(tp)) {
                tp->prev = merge_at(priv, cmp, tp->prev);
            } else {
                tp = merge_at(priv, cmp, tp);
            }
        } else if (run_size(tp->prev) <= run_size(tp)) {
            tp = merge_at(priv, cmp, tp);
        } else {
            break;
        }
    }

    return tp;
}

__attribute__((nonnull(2, 3))) void timsort(void *priv, struct list_head *head, list_cmp_func_t cmp)
{
    stk_size = 0;

    struct list_head *list = head->next, *tp = NULL;
    if (head == head->prev)
        return;

    /* Convert to a null-terminated singly-linked list. */
    head->prev->next = NULL;

    do {
        /* Find next run */
        struct pair result = find_run(priv, list, cmp);
        result.head->prev = tp;
        tp = result.head;
        list = result.next;
        stk_size++;
        tp = merge_collapse(priv, cmp, tp);
    } while (list);

    /* End of input; merge together all the runs. */
    tp = merge_force_collapse(priv, cmp, tp);

    /* The final merge; rebuild prev links */
    struct list_head *stk0 = tp, *stk1 = stk0->prev;
    while (stk1 && stk1->prev)
        stk0 = stk0->prev, stk1 = stk1->prev;
    if (stk_size <= 1) {
        build_prev_link(head, head, stk0);
        return;
    }
    merge_final(priv, cmp, head, stk1, stk0);
}

/* End of timsort implementation*/

#define ARR_SIZE 1000000
static void sort(void *arg)
{
    char *name = arg;

    preempt_disable();
    struct list_head head;
    INIT_LIST_HEAD(&head);

    uint32_t r = getpid();

    for (int i = 0; i < ARR_SIZE; i++) {
        element_t *node = malloc(sizeof(element_t));
        node->value = (r = random_shuffle(r));
        list_add_tail(&node->list, &head);
    }
    preempt_enable();

    task_printf("[%s] %s: begin\n", name, __func__);
    task_printf("[%s] %s: start sorting\n", name, __func__);

    timsort(NULL, &head, cmp);

    element_t *node, *safe;
    list_for_each_entry_safe(node, safe, &head, list)
    {
        if (&safe->list != &head && node->value > safe->value) {
            task_printf("[%s] %s: failed: node->value=%u, safe->value=%u\n", name, __func__,
                        node->value, safe->value);
            abort();
        }
    }

    task_printf("[%s] %s: end\n", name, __func__);

    preempt_disable();
    struct list_head *pos, *tmp;
    list_for_each_safe (pos, tmp, &head) {
        element_t *node = list_entry(pos, element_t, list);
        free(node);
    }
    preempt_enable();
}

int main()
{
    timer_init();
    task_init();

    task_add(sort, "1"), task_add(sort, "2"), task_add(sort, "3");

    preempt_disable();
    timer_create(10000); /* 10 ms */

    while (!list_empty(&task_main.list) || !list_empty(&task_reap)) {
        preempt_enable();
        timer_wait();
        preempt_disable();
    }

    preempt_enable();
    timer_cancel();

    return 0;
}
