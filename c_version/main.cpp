#include <iostream>
#include <unistd.h>

#include "thread_pool.h"

#define MAX_THREAD  80
#define COUNTER_SIZE 1000

#define SAFE_DELETE(ptr) do { \
    if (nullptr != ptr) { delete ptr; ptr = nullptr; } \
} while(0)

void counter(task_t *task) {
    int index = *(int *)task->user_data;

    printf("index : %d, selfid : %lu\n", index, pthread_self());

    SAFE_DELETE(task->user_data);
    SAFE_DELETE(task);
}

int main() {
    thread_pool_t pool ;
    thread_pool_init(&pool, MAX_THREAD);

    for (int i = 0; i < COUNTER_SIZE; i++) {
        auto *task = new task_t;
        if (nullptr == task) {
            perror("new failed.");
            return -1;
        }
        memset(task, 0, sizeof(task_t));

        task->exec_task_func = counter;
        task->user_data = new int;
        *(int*)task->user_data = i;

        thread_pool_submit_task(&pool, task);
    }

    while (pool.task_count_in_queue != 0)
    {
        sleep(1);
    }

    if (0 != thread_pool_destroy(&pool))
    {
        printf("thread destroy failed.");
        return -1;
    }
    return 0;
}
