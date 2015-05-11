/*

	This is an example of a threadpool daemon. It's useful if you want to constantly process information as it arrives, but also sleep the threads when idle. 
	Copyright (C) 2015 Aaron Cowdin

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include <malloc.h>
#include <limits.h>

#if ( __WORDSIZE == 64 )
#define OS64BIT 1
#endif

//Queue States
#define FREE 0
#define READY 1
#define WORKING 2

//Thread States
#define RUN 10
#define EXIT 11

//WORKER Threads
#define QUEUEMULTIPLIER 10 //How many queue slots per thread

unsigned int MAXTHREADS; //Total threads

typedef struct {
	pthread_mutex_t mutex; //mutex for giving threads ownership of a queue item.
	unsigned int owner_thread;
	unsigned int state;
	unsigned int success;
	unsigned long long int fibonacci;
} queue;

//This is how we track the state of each thread, looking for dead/struck threads, etc.
typedef struct {
	time_t lastexecution;
	unsigned int jobstaken;
	unsigned int totalexecutionseconds;
	unsigned int longestexecution;
	unsigned int sleeping;
} threaddata;

typedef struct {
	unsigned int state;

	pthread_t *threads;
	pthread_mutex_t mutex;      // protects all vars declared below.
	pthread_cond_t job_posted; // dispatcher: "Hey guys, there's a job!"
	pthread_cond_t job_taken;  // a worker: "Got it!"

	int numthreads;      // Number of entries in array.
	int livethreads;       // Number of live threads in pool (when
	int numqueue;
	queue *workQueue;
} threadpool;

extern  int alphasort();
pthread_mutex_t mutex1;
time_t lastmysqlrun;

void PutDataIntoQueue(threadpool *pool);
unsigned int InsertJob(unsigned int fibonacci, threadpool *pool, unsigned int exitprotocol);

void *PrintFibonacci(void *args);
int isJobAvailable(queue *workQueue, int numqueue);

int main(int argc, char** argv){
	unsigned int z=0;
	pthread_attr_t attr;
	
	if(argc < 2){
		MAXTHREADS = 4;
		fprintf(stderr,"Setting number of threads to 4.\nTo specify: %s [num threads]\n", argv[0]);
	} else {
		MAXTHREADS = atoi(argv[1]); 
	}

	//Initialize threadpool 
	threadpool *pool;
	pool = (threadpool *) malloc(sizeof(threadpool));

	pthread_mutex_init(&(pool->mutex), NULL);
	pthread_cond_init(&(pool->job_posted), NULL);
	pthread_cond_init(&(pool->job_taken), NULL);

	pool->numthreads = MAXTHREADS;
	pool->numqueue = MAXTHREADS * QUEUEMULTIPLIER;

	pool->state = RUN;

	//Initialize work queue
	pool->workQueue = (queue *) malloc ( pool->numqueue * sizeof(queue));
	for(z=0;z<pool->numqueue;z++){
		pool->workQueue[z].state = FREE;
		pool->workQueue[z].owner_thread = -1;
		pthread_mutex_init(&(pool->workQueue[z].mutex), NULL);
	}

	//Create detatched so resources are returned at thread exit
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	//Initialize Threads
	pool->threads = (pthread_t *) malloc (pool->numthreads * sizeof(pthread_t));
	for(z=0;z<MAXTHREADS;z++){
		if (pthread_create(pool->threads + z, &attr, PrintFibonacci, (void *) pool)) {
			perror("\n\nThread creation failed:");
			exit(EXIT_FAILURE);
		}
	}

	fprintf(stderr,"Hey, just so you know, this is going to run until you Cntrl-C or kill it.\n");
	sleep(5);

    while (1){
        PutDataIntoQueue(pool);
        usleep(250);
    }

	pthread_attr_destroy(&attr);
    return 0;
}

/*
	Typically this function is where you would gather up data to be processed. For instance, look in a directory for images or a databases for updates, etc. For this we're just going to send the fibonacci sequence to max unsigned 32 bit int limit into the processor, because it's worthless but sounds impressive.
*/
void PutDataIntoQueue(threadpool *pool){
	int x, first = 0, second = 1, next;
 
#if OS64BIT
	for ( x = 0; x <= 93; x++){
#else
	for ( x = 0; x <= 47; x++){
#endif
		if (x <= 1){
			next = x;
		} else {
			next = first + second;
			first = second;
			second = next;
		}
		while(!InsertJob(next, pool, 0)){
			usleep(100); //Sleep just a bit if the queue is full.
		}
   }


	return;
}	

unsigned int InsertJob(unsigned int fibonacci, threadpool *pool, unsigned int exitprotocol){
    int inserted = 0, q = 0;

	//Exit gracefully
	if(exitprotocol){
		fprintf(stderr,"Destroyer: Exit Protocol Engaged - Waiting for all threads to end.\n");

		pool->state = EXIT; //Set the system state to exit so threads know what to do.
		while (pool->livethreads > 0) {
			// get workers to check in ...
			fprintf(stderr, "Destroyer: Wake thread\n");
			pthread_cond_signal(&pool->job_posted);
			// ... and wake up when they check out.
			pthread_cond_wait(&pool->job_taken, &pool->mutex);
			fprintf(stderr, "Destroyer: received 'job_taken'. threads left = %d\n", pool->livethreads);
			pthread_mutex_unlock(&pool->mutex);
		}

		// Null-out entries in pool's thread array; free array.
		memset(pool->threads, 0, pool->numthreads * sizeof(pthread_t));
		free(pool->threads);

		fprintf(stderr, "Destroyer: All threads destroyed.\n");

		return 1;
	}

	for (q = 0; q < pool->numqueue; q++){
		queue *this_queue = &pool->workQueue[q];

		if(this_queue->state == READY){
			pthread_cond_signal(&pool->job_posted); //Something's in the queue and maybe got skipped, signal to run.
		} else if (!inserted && this_queue->state == FREE && !pthread_mutex_trylock(&this_queue->mutex)){
			this_queue->fibonacci = fibonacci;
			this_queue->state = READY; //Queue item is Loaded, set READY
			pthread_mutex_unlock(&this_queue->mutex); //This unlocks the trylock up there in the else if
			pthread_cond_signal(&pool->job_posted); //Signal to the kernal that a thread should wake up and do something. 

			inserted = 1;
		/*
        } else if (this_queue->state == DONE && !pthread_mutex_trylock(&this_queue->mutex)){
            pthread_mutex_unlock(&this_queue->mutex);

			You can use this kind of structure to handle post thread processing if it has to be done outside of the thread.

		*/
        } else {
			//This is a nice place to check for long working threads or other problems.
		}
    }

    return inserted;
}

void *PrintFibonacci(void *args){
	int thread_id=-1;

	//This is the thread initialization
	threadpool *p= (threadpool *)args;
	pthread_mutex_lock(&p->mutex);
	thread_id = p->livethreads;
	p->livethreads = p->livethreads+1;
	pthread_mutex_unlock(&p->mutex);

	fprintf(stderr,"Thread[%d] says,\"I'm Alive!\"\n", thread_id);

	//Loop forever
	for( ; ; ) {
		int queue_entry = -1;
		int ready_queue = 0;
    
		//Wait for a job in the queue
		while(!isJobAvailable(p->workQueue, p->numqueue) && p->state != EXIT) {
            pthread_mutex_lock(&p->mutex);
            pthread_cond_wait(&p->job_posted, &p->mutex); //This is the most important part for the daemon. Makes sure you're not burning cycles when there's nothing to do.
            pthread_mutex_unlock(&p->mutex);
		}

		// We've just woken up and we have the mutex.  Check pool's state
		if (p->state == EXIT)
			break;

		// Find a queue entry index to work on. Also, try to grab the mutex.
		for(queue_entry=0;queue_entry<p->numqueue;queue_entry++){
			if(p->workQueue[queue_entry].state==READY && !pthread_mutex_trylock(&p->workQueue[queue_entry].mutex)){
				p->workQueue[queue_entry].state=WORKING;
				ready_queue = 1;
				//Set state to working
				break;
			}
		}

		//We've got one. Begin working. 
		if(ready_queue){
			queue *this_job = &p->workQueue[queue_entry];
			this_job->success = 0;

			pthread_cond_signal(&p->job_taken);
    
			// Run the job we've taken
			fprintf(stderr,"Thread[%d] Fibonacci Number is %lld\n", thread_id, this_job->fibonacci);

			this_job->fibonacci = 0; //Clean queue up for next operation
            this_job->state = FREE;

			if (pthread_mutex_unlock(&this_job->mutex)) {
				fprintf(stderr, "Mutex lock failed!: Thread[%d] Dead!\n", thread_id);
				exit(1);
			}
		} else {
			//Maybe the queue is empty or the job was already taken by another thread. Either way, go to sleep."
			fprintf(stderr, "Thread[%d] missed job. Carry on.\n", thread_id);
		}

		//fprintf(stderr, "Thread[%d] job done, sleep.\n", thread_id);
	}
  	
	fprintf(stderr, "Thread[%d] exiting.\n", thread_id);

	//Exit protocol engaged.
	--p->livethreads;
	// We're not really taking a job ... but this signals the destroyer that one thread has exited, so it can keep on destroying.
	pthread_cond_signal(&p->job_taken);
	if (pthread_mutex_unlock(&p->mutex)) {
		fprintf(stderr, "Mutex unlock failed!: Thread[%d]\n", thread_id);
		exit(1);
	}

	pthread_exit((void*) args);
}

int isJobAvailable(queue *workQueue, int numqueue) {
	int i=0;
	int availible=0;
	
	for(i=0;i<numqueue;i++){
		if(workQueue[i].state==READY){
			availible = 1;
			break;
		}
	}
	
	return availible;
}
