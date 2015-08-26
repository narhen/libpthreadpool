#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <pthreadpool.h>

#define DROP_THREAD ((void (*)(void *))-1)

struct job_list {
	struct job_list *next, *prev;
	struct job *job;
};

struct job {
	void (*task)(void *arg);
	void *data;
};

struct thread {
	struct thread *next, *prev;
	int id;
	pthread_t thread;
	struct pthreadpool *pool;
};

struct pthreadpool {
	volatile int run; /* set to 0 if threads are to terminate */
	volatile int num_free_jobs;
	volatile int jobs_in_progress;
	struct job_list jobs;

	pthread_mutex_t lock; // used when accessing any member of this struct
	pthread_cond_t cond; // used when threads are waiting for jobs
	pthread_mutex_t pause_lock; // used when threads are paused
	pthread_cond_t pause_cond; // used when threas are paused
	struct thread threads;
	int num_threads;

	int max_thread_id;
};

/* a thread local variable which holds the threads own thread struct */
static __thread struct thread *thread_data;

static struct thread *get_thread_struct(void)
{
	return thread_data;
}

/* must be called in a locked context */
static void add_tail(struct job_list *list, struct job_list *new)
{
	struct job_list *tail;

	tail = list->prev;
	new->next = tail->next;
	new->prev = tail;
	tail->next = new;
	list->prev = new;
}

/* must be called in a locked context */
static void add_head(struct job_list *list, struct job_list *new)
{
	new->next = list->next;
	new->prev = list;
	list->next->prev = new;
	list->next = new;
}

/* must be called in a locked context */
static void unlink_node(struct job_list *j)
{
	j->next->prev = j->prev;
	j->prev->next = j->next;
}

static struct job *get_job(struct pthreadpool *pool)
{
	struct job_list *tmp;
	struct job *ret = NULL;

	pthread_mutex_lock(&pool->lock);
	while (pool->num_free_jobs == 0 && pool->run)
		pthread_cond_wait(&pool->cond, &pool->lock);

	if (pool->run && pool->num_free_jobs > 0) {
		tmp = pool->jobs.next;
		unlink_node(tmp);
		ret = tmp->job;
		free(tmp);

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
	struct job_list *ptr, *next;

	pthread_mutex_lock(&pool->lock);

	for (ptr = pool->jobs.next; ptr != &pool->jobs; ptr = next) {
		next = ptr->next;

		if (ptr->job->task != DROP_THREAD) {
			unlink_node(ptr);
			free(ptr->job);
			free(ptr);
		}
	}

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

	pthread_mutex_lock(&thinfo->pool->lock);
	thinfo->pool->num_threads--;
	pthread_mutex_unlock(&thinfo->pool->lock);

	return NULL;
}

static int add_threads(struct pthreadpool *pool, int num)
{
	int i;
	struct thread *thread_tmp, *tmp;

	tmp = &pool->threads;
	for (i = 0; i < num; i++) {
		thread_tmp = malloc(sizeof(struct thread));
		thread_tmp->id = pool->max_thread_id++;
		thread_tmp->pool = pool;

		/* add the new thread to the threadlist */
		thread_tmp->next = tmp;
		thread_tmp->prev = tmp->prev;
		tmp->prev->next = thread_tmp;
		tmp->prev = thread_tmp;

		pthread_create(&thread_tmp->thread, NULL, loiter, (void *)thread_tmp);
	}

	pool->num_threads += i;
	return i;
}

#define TAIL 1
#define HEAD 0
static void _put_job(struct pthreadpool *pool, struct job *j, int where)
{
	struct job_list *new;

	pthread_mutex_lock(&pool->lock);
	new = malloc(sizeof(struct job_list));
	new->job = j;

	if (where != HEAD)
		add_tail(&pool->jobs, new);
	else
		add_head(&pool->jobs, new);
	++pool->num_free_jobs;

	pthread_mutex_unlock(&pool->lock);
	pthread_cond_signal(&pool->cond);
}

/* Initialize a new threadpool */
struct pthreadpool *pthreadpool_init(int pool_size)
{
	struct pthreadpool *ret;

	ret = malloc(sizeof(struct pthreadpool));
	ret->run = 1;
	ret->num_free_jobs = 0;
	ret->jobs_in_progress = 0;
	ret->num_threads = 0;
	ret->max_thread_id = 1;

	thread_data = NULL;

	ret->jobs.next = ret->jobs.prev = &ret->jobs;
	ret->threads.next = ret->threads.prev = &ret->threads;
	pthread_mutex_init(&ret->lock, NULL);
	pthread_cond_init(&ret->cond, NULL);

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
 * @param num_threads is the number of threads to add to the pool
 * Returns the number of threads to be removed
 */
int pthreadpool_less_threads(struct pthreadpool *pool, int num_threads)
{
	int i;
	struct job *job;

	if (num_threads <= 0)
		return 0;

	if (num_threads > pool->num_threads)
		num_threads = pool->num_threads;

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
int pthreadpool_in_progess(struct pthreadpool *pool)
{
	return pool->jobs_in_progress;
}

/* Returns the number of threads currently in the pool
*/
int pthreadpool_size(struct pthreadpool *pool)
{
	return pool->num_threads;
}

/* Frees all resources allocated by the threadpool, including all threads and
 * the thread pool itself */
void pthreadpool_shutdown(struct pthreadpool *pool, int force_kill)
{
	struct job_list *ptr, *next;
	struct thread *tmp, *tmpnext;

	pool->run = 0;
	pthread_cond_broadcast(&pool->cond);

	for (tmp = pool->threads.next; tmp != &pool->threads; tmp = tmpnext) {
		tmpnext = tmp->next;
		tmp->next->prev = tmp->prev;
		tmp->prev->next = tmp->next;

		if (force_kill)
			pthread_cancel(tmp->thread);
		pthread_join(tmp->thread, NULL);

		free(tmp);
	}
	pthread_mutex_destroy(&pool->lock);

	// clean up after any left over jobs
	for (ptr = pool->jobs.next; ptr != &pool->jobs; ptr = next) {
		next = ptr->next;
		free(ptr);
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
	struct thread *ptr;

	pthread_mutex_lock(&pool->lock);

	for (ptr = pool->threads.next; ptr != &pool->threads; ptr = ptr->next)
		pthread_kill(ptr->thread, SIGUSR1);

	pthread_mutex_unlock(&pool->lock);
}

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
