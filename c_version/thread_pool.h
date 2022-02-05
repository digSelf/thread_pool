#pragma once

#include <pthread.h>

struct task_t
{
    void (*exec_task_func)(task_t *);
    void *user_data;
    struct task_t *prev;
    struct task_t *next;
};

struct worker_t
{
    pthread_t thread_handle;
    bool termiante;
    struct manager_t *curr_pool;
    struct worker_t *prev;
    struct worker_t *next;
};

typedef struct manager_t
{
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    size_t task_count_in_queue;
    size_t worker_thread_count;
    struct task_t *task_queue;
    struct worker_t *worker_queue;
} thread_pool_t;

// 使用的函数
static void *thread_callback(void *arg);

int thread_pool_init(thread_pool_t *pool, int worker_num=1);
int thread_pool_destroy(thread_pool_t *pool);

/**
 * 向线程池中提交任务
 * @param pool 指向线程池的指针
 * @param task 指向当前要提交的任务结构体的指针，这个指针可以是指向栈变量的指针，函数内部会自动申请一块内存来进行保存
 * @return 提交成功返回0，否则返回值小于0
 */
int thread_pool_submit_task(thread_pool_t *pool, struct task_t *task);