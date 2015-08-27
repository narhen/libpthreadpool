#ifndef __THREADPOOL_H
#define __THREADPOOL_H

struct pthreadpool;

extern struct pthreadpool *pthreadpool_init(int pool_size);
extern void pthreadpool_put(struct pthreadpool *pool, void (*task)(void *), void *data);
extern int pthreadpool_remaining(struct pthreadpool *pool);
extern int pthreadpool_in_progress(struct pthreadpool *pool);
extern void pthreadpool_shutdown(struct pthreadpool *pool, int force_kill);
extern int pthreadpool_more_threads(struct pthreadpool *pool, int num_threads);
extern int pthreadpool_less_threads(struct pthreadpool *pool, int num_threads);
extern int pthreadpool_size(struct pthreadpool *pool);
extern void pthreadpool_reset(struct pthreadpool *pool);
extern void pthreadpool_pause(struct pthreadpool *pool);
extern void pthreadpool_resume(struct pthreadpool *pause);
extern int pthreadpool_get_tid(void);

#endif
