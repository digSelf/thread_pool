//
// Created by AlvinLi on 2022/2/5.
//

#include "thread_pool.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <unistd.h>



#define SAFE_DELETE(ptr) do { \
    if (nullptr != ptr) { delete ptr; ptr = nullptr; } \
} while(0)

#define LIST_INSERT_AT_HEAD(node, list) do { \
    assert(node != nullptr);         \
    assert(list != nullptr);         \
    node->next = list->next;         \
    node->prev = list;               \
    list->next->prev = node;         \
    list->next = node;               \
} while(0)

#define LIST_REMOVE_AT_HEAD(node, list) do { \
    assert(node != nullptr);         \
    assert(list != nullptr);         \
    list->next = node->next;         \
    node->next->prev = node->prev;   \
    node->next = nullptr;            \
    node->prev = nullptr;            \
} while(0)

#define REMOVE_NODE(node) do { \
    assert(node != nullptr);   \
    node->prev->next = node->next; \
    node->next->prev = node->prev; \
    node->next = nullptr;      \
    node->prev = nullptr;       \
} while(0)

static void *thread_callback(void *arg)
{
    auto *worker = (worker_t*)arg;

    while (true)
    {
        auto *pool = worker->curr_pool;
        pthread_mutex_lock(&pool->mutex);
        while (pool->task_count_in_queue == 0)
        {
            // 如果当前要退出当前线程
            if (worker->termiante)
            {
                goto LBL_EXIT;
            }
            pthread_cond_wait(&pool->cond, &pool->mutex);

        }

        // 如果当前任务不为空，则获取任务队列中的队首任务，去执行，并将任务队列中的任务个数减去1
        task_t *head_task_in_queue = pool->task_queue->next;
        // 从当前队列中移除当前节点
        LIST_REMOVE_AT_HEAD(head_task_in_queue, pool->task_queue);
        pool->task_count_in_queue--;
        // 操作完临界资源，立即释放互斥锁
        pthread_mutex_unlock(&pool->mutex);

        head_task_in_queue->exec_task_func(head_task_in_queue);
    }

LBL_EXIT:
    // 在解锁之前，将当前工作线程从工作线程队列中减去
    REMOVE_NODE(worker);
    pthread_mutex_unlock(&worker->curr_pool->mutex);
    // 在解锁之前，将线程中的工作线程的数量减去1
    worker->curr_pool->worker_thread_count--;
    
    // 释放传进来的内存资源
    SAFE_DELETE(worker);
    pthread_exit(nullptr);
    return nullptr;
}

int thread_pool_init(thread_pool_t *pool, int worker_num)
{
    if (nullptr == pool) return -1;
    if (worker_num < 1)  // 如果设置的工作线程数小于1，则设置为1，默认情况下为1个工作线程
        worker_num = 1;

    memset(pool, 0, sizeof(thread_pool_t));
    pool->worker_thread_count = worker_num;

    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    memcpy(&pool->cond, &cond, sizeof(pthread_cond_t));
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    memcpy(&pool->mutex, &mutex, sizeof(pthread_mutex_t));

    // 对两个队列均构造两个dummy node，分别为dummy head, dummy tail
    auto *task_dummy_head = new struct task_t;
    assert(task_dummy_head != nullptr);
    memset(task_dummy_head, 0, sizeof(task_t));
    auto *task_dummy_tail = new struct task_t;
    assert(task_dummy_tail != nullptr);
    memset(task_dummy_tail, 0, sizeof(task_t));
    pool->task_queue = task_dummy_head;

    auto *worker_dummy_head = new struct worker_t;
    assert(worker_dummy_head != nullptr);
    memset(worker_dummy_head, 0, sizeof(worker_t));
    auto *worker_dummy_tail = new struct worker_t;
    assert(worker_dummy_tail != nullptr);
    memset(worker_dummy_tail, 0, sizeof(worker_t));
    pool->worker_queue = worker_dummy_head;

    // 分别链接两个双链表
    task_dummy_head->next = task_dummy_tail;
    task_dummy_tail->prev = task_dummy_head;

    worker_dummy_head->next = worker_dummy_tail;
    worker_dummy_tail->prev = worker_dummy_head;

    // 创建工作线程
    for (int i = 0; i < worker_num; ++i)
    {
        auto *worker = new worker_t;
        assert(nullptr != worker);
        memset(worker, 0, sizeof(worker_t));

        worker->curr_pool = pool;
        int ret = pthread_create(&worker->thread_handle, nullptr, thread_callback, worker);
        if (0 != ret)
        {
            perror("create thread failed.");
            return -2;
        }
        // 开始插入工作线程链表中，头插法
        LIST_INSERT_AT_HEAD(worker, worker_dummy_head);
    }

    return 0;
}

int thread_pool_destroy(thread_pool_t *pool)
{
    // 获得dummy head node
    worker_t *worker_cursor = pool->worker_queue->next;
    for (int i = 0; i < pool->worker_thread_count; ++i)
    {
        worker_cursor->termiante = true;
        worker_cursor = worker_cursor->next;
    }

    pthread_mutex_lock(&pool->mutex);
    // 进行广播，让所有的线程的条件变量均成立
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    // 当线程池中的工作线程数量为0时，释放线程队列中的两个哨兵节点
    while (pool->worker_thread_count)
    {
        sleep(1);
    }
    worker_t *worker_dummy_tail = pool->worker_queue->next;
    SAFE_DELETE(pool->worker_queue);
    SAFE_DELETE(worker_dummy_tail);

    // 释放任务队列中的所有还未执行的任务
    task_t *task_cursor = pool->task_queue->next;
    while (task_cursor->next != nullptr)
    {
        REMOVE_NODE(task_cursor);
        SAFE_DELETE(task_cursor->user_data);
        SAFE_DELETE(task_cursor);
        task_cursor = pool->task_queue->next;
    }
    // 删除任务队列中的哨兵头结点和哨兵尾节点
    SAFE_DELETE(pool->task_queue->next);
    SAFE_DELETE(pool->task_queue);

    pool->task_count_in_queue = 0;

    // 销毁mutex和cond
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);

    return 0;
}

int thread_pool_submit_task(thread_pool_t *pool, struct task_t *task, bool is_backup)
{
    pthread_mutex_lock(&pool->mutex);

    if (is_backup)
    {
        auto *bak = new task_t;
        assert(bak != nullptr);
        memcpy(bak, task, sizeof(task_t));
        task = bak;
    }
    LIST_INSERT_AT_HEAD(task, pool->task_queue);
    pool->task_count_in_queue ++;
    pthread_cond_signal(&pool->cond);

    pthread_mutex_unlock(&pool->mutex);
    return 0;
}
