/* pool.c - 固定线程池：每个工作线程拥有自己的开始/完成事件，
 * 主线程只做“派活 + 等齐”，同步开销固定，不随帧数增长。 */
#include "pool.h"

#include <windows.h>
#include <stdlib.h>

typedef struct {
    struct ThreadPool *pool;
    int index;
    HANDLE go;
    HANDLE done;
    int row0;
    int row1;
} Worker;

struct ThreadPool {
    int count;
    int rows;
    PoolFn fn;
    void *user;
    Worker *workers;
    HANDLE *threads;
    volatile LONG quit;
};

static DWORD WINAPI pool_worker_main(LPVOID arg)
{
    Worker *w = (Worker *)arg;
    ThreadPool *p = w->pool;
    for (;;) {
        WaitForSingleObject(w->go, INFINITE);
        if (p->quit) break;
        if (w->row1 > w->row0 && p->fn) p->fn(p->user, w->row0, w->row1);
        SetEvent(w->done);
    }
    return 0;
}

ThreadPool *pool_create(int threads)
{
    ThreadPool *p;
    SYSTEM_INFO si;
    int i;

    if (threads <= 0) {
        GetSystemInfo(&si);
        threads = (int)si.dwNumberOfProcessors;
        if (threads < 1) threads = 1;
    }
    if (threads > DP_POOL_MAX) threads = DP_POOL_MAX;

    p = (ThreadPool *)calloc(1, sizeof(ThreadPool));
    if (!p) return NULL;
    p->count = threads;
    p->workers = (Worker *)calloc((size_t)threads, sizeof(Worker));
    p->threads = (HANDLE *)calloc((size_t)threads, sizeof(HANDLE));
    if (!p->workers || !p->threads) { pool_destroy(p); return NULL; }

    if (threads == 1) return p;

    for (i = 0; i < threads; ++i) {
        Worker *w = &p->workers[i];
        w->pool = p;
        w->index = i;
        w->go = CreateEventW(NULL, FALSE, FALSE, NULL);
        w->done = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!w->go || !w->done) { pool_destroy(p); return NULL; }
        p->threads[i] = CreateThread(NULL, 0, pool_worker_main, w, 0, NULL);
        if (!p->threads[i]) { pool_destroy(p); return NULL; }
    }
    return p;
}

void pool_destroy(ThreadPool *p)
{
    int i;
    if (!p) return;
    if (p->workers && p->count > 1) {
        p->quit = 1;
        for (i = 0; i < p->count; ++i)
            if (p->workers[i].go) SetEvent(p->workers[i].go);
        for (i = 0; i < p->count; ++i)
            if (p->threads[i]) WaitForSingleObject(p->threads[i], INFINITE);
    }
    if (p->threads) {
        for (i = 0; i < p->count; ++i)
            if (p->threads[i]) CloseHandle(p->threads[i]);
        free(p->threads);
    }
    if (p->workers) {
        for (i = 0; i < p->count; ++i) {
            if (p->workers[i].go) CloseHandle(p->workers[i].go);
            if (p->workers[i].done) CloseHandle(p->workers[i].done);
        }
        free(p->workers);
    }
    free(p);
}

void pool_run(ThreadPool *p, PoolFn fn, void *user, int rows)
{
    int i;
    int per;
    if (!p) return;
    if (p->count <= 1) {
        if (fn) fn(user, 0, rows);
        return;
    }
    p->fn = fn;
    p->user = user;
    p->rows = rows;
    per = (rows + p->count - 1) / p->count;
    for (i = 0; i < p->count; ++i) {
        Worker *w = &p->workers[i];
        w->row0 = i * per;
        w->row1 = w->row0 + per;
        if (w->row1 > rows) w->row1 = rows;
        if (w->row0 > rows) w->row0 = rows;
        SetEvent(w->go);
    }
    for (i = 0; i < p->count; ++i)
        WaitForSingleObject(p->workers[i].done, INFINITE);
}

int pool_threads(const ThreadPool *p) { return p ? p->count : 0; }
