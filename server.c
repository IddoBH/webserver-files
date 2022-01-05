#include "segel.h"
#include "request.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>

#define INFINATE_LOOP while(1)

//globals

extern struct timeval request_clock;

extern int threads;
extern pthread_t* thread_ids;

int q_size;
int* request_queue;
int head = 0;
int tail = 0;




enum schedalg {BLOCK, DROP_HEAD, DROP_RANDOM, DROP_TAIL} chosen_alg;


pthread_cond_t condition;
pthread_mutex_t mutex;

//
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// HW3: Parse the new arguments too
void getargs(int *port, int *threads, int argc, char *argv[])
{
    if (argc < 5) {
	fprintf(stderr, "Usage: %s <port> <threads> <queue_size> <schedalg>\n", argv[0]);
	exit(1);
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    q_size = atoi(argv[3]);
    if (strcmp(argv[4], "block") == 0) chosen_alg = BLOCK;
    else if (strcmp(argv[4], "dh") == 0) chosen_alg = DROP_HEAD;
    else if (strcmp(argv[4], "random") == 0) chosen_alg = DROP_RANDOM;
    else if (strcmp(argv[4], "dt") == 0) chosen_alg = DROP_TAIL;
}


bool queueIsEmpty() { return head == tail; }

int dequeue() {
    pthread_mutex_lock(&mutex);
    while (queueIsEmpty()) {
        pthread_cond_wait(&condition, &mutex);
    }

    int next = request_queue[(head++) % q_size];

    pthread_mutex_unlock(&mutex);
    return next;
}

void *work(void *vargp)
{
    int *myid = (int *)vargp;
    INFINATE_LOOP
    {
        int connfd = dequeue();

        requestHandle(connfd, myid);

        Close(connfd);
    }

    return 0;
}

void enqueue(int fd) {
    pthread_mutex_lock(&mutex);

    request_queue[(tail++) % q_size] = fd;

    pthread_cond_signal(&condition);
    pthread_mutex_unlock(&mutex);
}

bool queueIsFull() { return tail - head == q_size; }

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen;
    struct sockaddr_in clientaddr;

    pthread_cond_init(&condition, NULL);
    pthread_mutex_init(&mutex, NULL);

    getargs(&port, &threads, argc, argv);

    //
    // HW3: Create some threads...
    //
    request_queue = malloc(q_size * sizeof(int));

    thread_ids = malloc(threads * sizeof(pthread_t));
    for (int i=0; i<threads; i++) {
        pthread_create(&thread_ids[i], NULL, work, NULL);
    }

    listenfd = Open_listenfd(port);
    INFINATE_LOOP {
	    clientlen = sizeof(clientaddr);
        if (chosen_alg == BLOCK) {
            pthread_mutex_lock(&mutex);
            while (queueIsFull()){
                pthread_cond_wait(&condition, &mutex);
            }
            pthread_cond_signal(&condition);
            pthread_mutex_unlock(&mutex);
        }
	    connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
        gettimeofday(&request_clock, NULL);

	//
	// HW3: In general, don't handle the request in the main thread.
	// Save the relevant info in a buffer and have one of the worker threads
	// do the work.
	//
        switch (chosen_alg) {
            case BLOCK:
                enqueue(connfd);
                break;
            case DROP_HEAD:
                enqueue(connfd);
                break;
            case DROP_RANDOM:
                if (queueIsFull()){
                    int half_q = q_size / 2;
                    for (int i = 0; i < half_q; ++i){
                        int to_remove = rand() % (tail - head);
                        for (int j = to_remove; (j % q_size) != (tail % q_size); ++j) {
                            request_queue[j] = request_queue[(j + 1) % q_size];
                        }
                        --tail;
                    }
                }
                enqueue(connfd);
                break;
            case DROP_TAIL:
                if (queueIsFull()) --tail;
                enqueue(connfd);
                break;
        }
    }
    free(thread_ids);
    free(request_queue);
    for (unsigned int i=0; i<threads; i++)
        pthread_join(thread_ids[i], NULL);
}
