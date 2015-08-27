#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <pthreadpool.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void increment(void *arg)
{
	int *c = (int *)arg;

	pthread_mutex_lock(&mutex);
	++*c;
	pthread_mutex_unlock(&mutex);
}

int main(int argc, char *argv[])
{
	int nr_threads, nr_jobs;
	int i, count = 0;
	struct pthreadpool *pool;

	if (argc != 3) {
		printf("Usage: %s <nr threads> <nr jobs>\n", argv[0]);
		return 1;
	}

	nr_threads = atoi(argv[1]);
	nr_jobs = atoi(argv[2]);

	pool = pthreadpool_init(nr_threads);

	for (i = 0; i < nr_jobs; i++)
		pthreadpool_put(pool, increment, &count);

	while (pthreadpool_remaining(pool) > 0 || pthreadpool_in_progress(pool) > 0)
		usleep(100);

	pthreadpool_shutdown(pool, 0);

	printf("count = %d (expected %d)\n", count, nr_jobs);

	return 0;
}
