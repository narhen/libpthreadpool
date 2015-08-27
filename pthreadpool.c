#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include <pthreadpool.h>
#include <list.h>

#define DROP_THREAD ((void (*)(void *))-1)
#define TO_THREAD(x) ((struct thread *)(x))
#define TO_JOB(x) ((struct job *)(x))

struct job {
	void (*task)(void *arg);
	void *data;
};

struct thread {
	int id;
	pthread_t thread;
	struct pthreadpool *pool;
};

struct pthreadpool {
	volatile int run; /* set to 0 if threads are to terminate */
	volatile int num_free_jobs;
	volatile int jobs_in_progress;
	struct list job_list;

	pthread_mutex_t lock; /* used when accessing members of this struct */
	pthread_cond_t cond; /* used when threads are waiting for jobs */
	pthread_mutex_t pause_lock; /* used when threads are paused */
	pthread_cond_t pause_cond; /* used when threas are paused */
	struct list threads;
	int num_threads;

	int max_thread_id;
};

/* a thread local variable which holds the threads own thread struct */
static __thread struct thread *thread_data;

static struct thread *get_thread_struct(void)
{
	return thread_data;
}

static struct job *get_job(struct pthreadpool *pool)
{
	struct list *lentry;
	struct job *ret = NULL;

	pthread_mutex_lock(&pool->lock);
	while (pool->num_free_jobs == 0 && pool->run)
		pthread_cond_wait(&pool->cond, &pool->lock);

	if (pool->run && pool->num_free_jobs > 0) {
		lentry = pool->job_list.next;
		list_remove(lentry);
		ret = TO_JOB(lentry->data);
		free(lentry);

		--pool->num_free_jobs;
	}

	pthread_mutex_unlock(&pool->lock);
	return ret;
}

static void report_start(struct pthreadpool *pool)
{
	pthread_mutex_lock(&pool->lock);
	++pool->jobs_in_progress;
	pthread_mutex_unlock(&pool->lock);
}

static void report_done(struct pthreadpool *pool)
{
	pthread_mutex_lock(&pool->lock);
	--pool->jobs_in_progress;
	pthread_mutex_unlock(&pool->lock);
}

static void clear_jobs_list(struct pthreadpool *pool)
{
	struct list *lptr, *tmp;
	struct job *job;

	pthread_mutex_lock(&pool->lock);

	list_for_each_tsafe(&pool->job_list, lptr, tmp) {
		job = TO_JOB(lptr->data);
		if (job->task != DROP_THREAD) {
			list_remove(lptr);
			free(job);
			free(lptr);
		}
	}

	pool->num_free_jobs = 0;
	pthread_mutex_unlock(&pool->lock);
}

static void pause_thread(int sig)
{
	struct thread *th = get_thread_struct();

	pthread_mutex_lock(&th->pool->pause_lock);
	pthread_cond_wait(&th->pool->pause_cond, &th->pool->pause_lock);
	pthread_mutex_unlock(&th->pool->pause_lock);
}

static void *loiter(void *arg)
{
	struct job *job;
	struct thread *thinfo = (struct thread *)arg;
	struct sigaction act;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	memset(&act, 0, sizeof(act));
	act.sa_handler = pause_thread;
	sigaction(SIGUSR1, &act, NULL);

	thread_data = thinfo;

	while (thinfo->pool->run) {
		job = get_job(thinfo->pool);
		if (job == NULL)
			break;

		report_start(thinfo->pool);
		if (job->task != DROP_THREAD)
			job->task(job->data);
		report_done(thinfo->pool);

		if (job->task == DROP_THREAD) {
			free(job);
			break;
		}
		free(job);
	}

	return NULL;
}

static int add_threads(struct pthreadpool *pool, int num)
{
	int i;
	struct list *lptr;
	struct thread *new;

	pthread_mutex_lock(&pool->lock);
	for (i = 0; i < num; i++) {
		new = malloc(sizeof(struct thread));
		new->id = pool->max_thread_id++;
		new->pool = pool;

		/* XXX add the new thread to the threadlist */
		lptr = malloc(sizeof(struct list));
		lptr->data = new;
		list_add_tail(&pool->threads, lptr);

		pthread_create(&new->thread, NULL, loiter, (void *)new);
	}

	pool->num_threads += i;
	pthread_mutex_unlock(&pool->lock);

	return i;
}

#define TAIL 1
#define HEAD 0
static void _put_job(struct pthreadpool *pool, struct job *j, int where)
{
	struct list *new;

	new = malloc(sizeof(struct list));
	new->data = j;

	pthread_mutex_lock(&pool->lock);
	if (where != HEAD)
		list_add_tail(&pool->job_list, new);
	else
		list_add_head(&pool->job_list, new);
	++pool->num_free_jobs;

	pthread_mutex_unlock(&pool->lock);
	pthread_cond_signal(&pool->cond);
}

/* Initialize a new threadpool */
struct pthreadpool *pthreadpool_init(int pool_size)
{
	struct pthreadpool *ret;

	if (pool_size < 0)
		return NULL;

	ret = malloc(sizeof(struct pthreadpool));
	ret->run = 1;
	ret->num_free_jobs = 0;
	ret->jobs_in_progress = 0;
	ret->num_threads = 0;
	ret->max_thread_id = 1;

	list_init(&ret->job_list);
	list_init(&ret->threads);

	pthread_mutex_init(&ret->lock, NULL);
	pthread_cond_init(&ret->cond, NULL);

	thread_data = NULL;
	add_threads(ret, pool_size);

	return ret;
}

/* Add more threads to a threadpool.
 * @param num_threads is the number of threads to add to the pool
 * Returns the number of threads added
 */
int pthreadpool_more_threads(struct pthreadpool *pool, int num_threads)
{
	if (num_threads <= 0)
		return 0;

	return add_threads(pool, num_threads);
}

/* Add more threads to a threadpool.
 * @param num_threads is the number of threads to remove from the pool
 * Returns the number of threads that are removed
 */
int pthreadpool_less_threads(struct pthreadpool *pool, int num_threads)
{
	int i, pool_size;
	struct job *job;

	if (num_threads <= 0)
		return 0;

	pthread_mutex_lock(&pool->lock);
	pool_size = pool->num_threads;

	if (num_threads > pool_size)
		num_threads = pool_size;

	pool->num_threads -= num_threads;
	pthread_mutex_unlock(&pool->lock);

	for (i = 0; i < num_threads; i++) {
		job = malloc(sizeof(struct job));
		job->task = DROP_THREAD;
		job->data = NULL;

		_put_job(pool, job, HEAD);
	}

	return i;
}

/* Put a new job into the pool */
void pthreadpool_put(struct pthreadpool *pool, void (*task)(void *), void *data)
{
	struct job *j;

	j = malloc(sizeof(struct job));

	j->task = task;
	j->data = data;

	_put_job(pool, j, TAIL);
}

/* Returns the number of jobs left to process */
int pthreadpool_remaining(struct pthreadpool *pool)
{
	return pool->num_free_jobs;
}

/* Returns the number of jobs in progress */
int pthreadpool_in_progress(struct pthreadpool *pool)
{
	return pool->jobs_in_progress;
}

/* Returns the number of threads currently in the pool
*/
int pthreadpool_size(struct pthreadpool *pool)
{
	int ret;

	ret = pool->num_threads;

	return ret;
}

/* Frees all resources allocated by the threadpool, including all threads and
 * the thread pool itself */
void pthreadpool_shutdown(struct pthreadpool *pool, int force_kill)
{
	struct list *lptr, *tmp;
	struct job *j;
	struct thread *th;

	pool->run = 0;
	pthread_cond_broadcast(&pool->cond);

	list_for_each_tsafe(&pool->threads, lptr, tmp) {
		list_remove(lptr);
		th = TO_THREAD(lptr->data);

		if (force_kill)
			pthread_cancel(th->thread);
		pthread_join(th->thread, NULL);

		free(th);
		free(lptr);
	}
	pthread_mutex_destroy(&pool->lock);

	/* clean up after any left over jobs */
	list_for_each_tsafe(&pool->job_list, lptr, tmp) {
		j = TO_JOB(lptr->data);
		free(j);
		free(lptr);
	}

	free(pool);
}

/* Empties all unfinished jobs currently in the pool */
void pthreadpool_reset(struct pthreadpool *pool)
{
	clear_jobs_list(pool);
}

/* Pauses the execution of all threads in the pool
*/
void pthreadpool_pause(struct pthreadpool *pool)
{
	struct list *lptr;

	pthread_mutex_lock(&pool->lock);

	list_for_each(&pool->threads, lptr)
		pthread_kill(TO_THREAD(lptr->data)->thread, SIGUSR1);

	pthread_mutex_unlock(&pool->lock);
}

/* Resume all paused threads
*/
void pthreadpool_resume(struct pthreadpool *pool)
{
	pthread_cond_broadcast(&pool->pause_cond);
}

/* Get the id of a thread.
 * The calling thread must be part of a thread pool.
 * Returns the id of the calling thread, or -1 if the thread is not in a
 * pool.
 */
int pthreadpool_get_tid(void)
{
	struct thread *th = get_thread_struct();

	return th != NULL ? th->id : -1;
}
