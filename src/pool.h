/* pool.h - 固定线程池：按行带切分，主线程等待全部完成 */
#ifndef DP_POOL_H
#define DP_POOL_H

#define DP_POOL_MAX 16

typedef void (*PoolFn)(void *user, int row0, int row1);
typedef struct ThreadPool ThreadPool;

ThreadPool *pool_create(int threads);
void        pool_destroy(ThreadPool *pool);
void        pool_run(ThreadPool *pool, PoolFn fn, void *user, int rows);
int         pool_threads(const ThreadPool *pool);

#endif