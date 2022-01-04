#include "segel.h"
#include "request.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define INFINATE_LOOP while(1)

//globals
int q_size;
int* request_queue;
int head = 0;
int tail = 0;

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
    if (argc < 4) {
	fprintf(stderr, "Usage: %s <port> <threads> <queue_size>\n", argv[0]);
	exit(1);
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    q_size = atoi(argv[3]);
}


int dequeue() {
    pthread_mutex_lock(&mutex);
    while (head == tail) {
        pthread_cond_wait(&condition, &mutex);
    }

    int next = request_queue[(head++) % q_size];

    pthread_mutex_unlock(&mutex);
    return next;
}

void *work(void *vargp)
{
    INFINATE_LOOP
    {
        int connfd = dequeue();

        requestHandle(connfd);

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

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, threads, clientlen;
    struct sockaddr_in clientaddr;

    pthread_cond_init(&condition, NULL);
    pthread_mutex_init(&mutex, NULL);

    getargs(&port, &threads, argc, argv);

    fprintf(stdout, "Port: %d\nThreads: %d\nQueue: %d\n", port, threads, q_size); // todo: delete before sub
    //
    // HW3: Create some threads...
    //
    request_queue = malloc(q_size * sizeof(int));

    pthread_t* thread_ids = malloc(threads * sizeof(pthread_t));
    for (unsigned int i=0; i<threads; i++)
        pthread_create(&thread_ids[i], NULL, work, NULL);


    listenfd = Open_listenfd(port);
    INFINATE_LOOP {
	    clientlen = sizeof(clientaddr);
	    connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);

	//
	// HW3: In general, don't handle the request in the main thread.
	// Save the relevant info in a buffer and have one of the worker threads
	// do the work.
	//
        enqueue(connfd);
    }
    free(thread_ids);
    free(request_queue);
    for (unsigned int i=0; i<threads; i++)
        pthread_join(thread_ids[i], NULL);
}
