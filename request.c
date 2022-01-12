//
// request.c: Does the bulk of the work for the web server.
// 

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "segel.h"
#include "request.h"
#include <pthread.h>
#include <sys/time.h>
#include <sys/queue.h>

struct request_t{
    int fd;
    struct timeval* arrival;
    struct timeval* dispatch;
    CIRCLEQ_ENTRY(request_t) pointers;
};

int get_req_fd(struct request_t* req){
    return req->fd;
}


CIRCLEQ_HEAD(request_queue, request_t) main_req_q = CIRCLEQ_HEAD_INITIALIZER(main_req_q);
unsigned int max_size;
unsigned int current_size;


void init_q() {
    CIRCLEQ_INIT(&main_req_q);
}


void set_arrival_time(struct request_t *new_req);

void push_request_queue(int fd, int overload) {
    struct request_t* new_req;
    new_req = malloc(sizeof(*new_req));

    new_req->fd = fd;
    if (overload) new_req->arrival = NULL;
    else{
        set_arrival_time(new_req);
    }
    if (CIRCLEQ_EMPTY(&main_req_q)){
//        fprintf(stderr, "SUP1\n");
        CIRCLEQ_INSERT_HEAD(&main_req_q, new_req, pointers);
    }
    else{
        CIRCLEQ_INSERT_TAIL(&main_req_q, new_req, pointers);
    }
    current_size++;
}

void set_arrival_time(struct request_t *new_req) {
    new_req->arrival = malloc(sizeof(*(new_req->arrival)));
    gettimeofday(new_req->arrival, NULL);
}

void set_dispatch_time(struct request_t *new_req) {
    new_req->dispatch = malloc(sizeof(*(new_req->dispatch)));
    gettimeofday(new_req->dispatch, NULL);
}

struct request_t *pop_request_queue() {
    struct request_t *next = CIRCLEQ_FIRST(&main_req_q);
    CIRCLEQ_REMOVE(&main_req_q, next, pointers);
    current_size--;
    struct request_t* iter;
    CIRCLEQ_FOREACH(iter, &main_req_q, pointers){
        if (iter->arrival == NULL){
            set_arrival_time(iter);
            break;
        }
    }
    set_dispatch_time(next);
    return next;
}


int threads;
pthread_t* thread_ids;

unsigned int request_counter = 0;
unsigned int static_counter = 0;
unsigned int dynamic_counter = 0;



void printMatrics(char *buf, struct request_t *req);
// requestError(      fd,    filename,        "404",    "Not found", "OS-HW3 Server could not find this file");
void requestError(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) 
{
   char buf[MAXLINE], body[MAXBUF];

   // Create the body of the error message
   sprintf(body, "<html><title>OS-HW3 Error</title>");
   sprintf(body, "%s<body bgcolor=""fffff"">\r\n", body);
   sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
   sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
   sprintf(body, "%s<hr>OS-HW3 Web Server\r\n", body);

   // Write out the header information for this response
   sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
   Rio_writen(fd, buf, strlen(buf));
   printf("%s", buf);

   sprintf(buf, "Content-Type: text/html\r\n");
   Rio_writen(fd, buf, strlen(buf));
   printf("%s", buf);

   sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
   Rio_writen(fd, buf, strlen(buf));
   printf("%s", buf);

   // Write out the content
   Rio_writen(fd, body, strlen(body));
   printf("%s", body);

}


//
// Reads and discards everything up to an empty text line
//
void requestReadhdrs(rio_t *rp)
{
   char buf[MAXLINE];

   Rio_readlineb(rp, buf, MAXLINE);
   while (strcmp(buf, "\r\n")) {
      Rio_readlineb(rp, buf, MAXLINE);
   }
   return;
}

//
// Return 1 if static, 0 if dynamic content
// Calculates filename (and cgiargs, for dynamic) from uri
//
int requestParseURI(char *uri, char *filename, char *cgiargs) 
{
   char *ptr;

   if (strstr(uri, "..")) {
      sprintf(filename, "./public/home.html");
      return 1;
   }

   if (!strstr(uri, "cgi")) {
      // static
      strcpy(cgiargs, "");
      sprintf(filename, "./public/%s", uri);
      if (uri[strlen(uri)-1] == '/') {
         strcat(filename, "home.html");
      }
      return 1;
   } else {
      // dynamic
      ptr = index(uri, '?');
      if (ptr) {
         strcpy(cgiargs, ptr+1);
         *ptr = '\0';
      } else {
         strcpy(cgiargs, "");
      }
      sprintf(filename, "./public/%s", uri);
      return 0;
   }
}

//
// Fills in the filetype given the filename
//
void requestGetFiletype(char *filename, char *filetype)
{
   if (strstr(filename, ".html")) 
      strcpy(filetype, "text/html");
   else if (strstr(filename, ".gif")) 
      strcpy(filetype, "image/gif");
   else if (strstr(filename, ".jpg")) 
      strcpy(filetype, "image/jpeg");
   else 
      strcpy(filetype, "text/plain");
}

void requestServeDynamic(int fd, char *filename, char *cgiargs, struct request_t *req)
{
    char buf[MAXLINE], *emptyCIRCLEQ[] = {NULL};



    // The server does only a little bit of the header.
    // The CGI script has to finish writing out the header.
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: OS-HW3 Web Server\r\n", buf);

    printMatrics(buf, req);

   Rio_writen(fd, buf, strlen(buf));

   if (Fork() == 0) {
      /* Child process */
      Setenv("QUERY_STRING", cgiargs, 1);
      /* When the CGI process writes to stdout, it will instead go to the socket */
      Dup2(fd, STDOUT_FILENO);
      Execve(filename, emptyCIRCLEQ, environ);
   }
   Wait(NULL);
}


void printMatrics(char *buf, struct request_t *req)
{
    if (req->arrival) {
        sprintf(buf, "%sStat-Req-Arrival:: %lu.%06lu\r\n", buf, req->arrival->tv_sec, req->arrival->tv_usec);
        long di_sec = req->dispatch->tv_sec-req->arrival->tv_sec;
        long di_usec = req->dispatch->tv_usec-req->arrival->tv_usec;
        if(di_usec < 0){
            di_sec--;
            di_usec = 1000000-di_usec;
        }
        sprintf(buf, "%sStat-Req-Dispatch:: %lu.%06lu\r\n",
                buf, di_sec, di_usec);
    }
    for (int i = 0; i < threads; ++i) {
        if (thread_ids[i] == pthread_self()) sprintf(buf, "%sStat-Thread-Id:: %d\r\n", buf, i);
    }
    sprintf(buf, "%sStat-Thread-Count:: %d\r\n", buf, request_counter);
    sprintf(buf, "%sStat-Thread-Static:: %d\r\n", buf, static_counter);
    sprintf(buf, "%sStat-Thread-Dynamic:: %d\r\n", buf, dynamic_counter);
}

void requestServeStatic(int fd, char *filename, int filesize, struct request_t *req)
{
   int srcfd;
   char *srcp, filetype[MAXLINE], buf[MAXBUF];

   requestGetFiletype(filename, filetype);

   srcfd = Open(filename, O_RDONLY, 0);

   // Rather than call read() to read the file into memory, 
   // which would require that we allocate a buffer, we memory-map the file
   srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
   Close(srcfd);

   // put together response
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: OS-HW3 Web Server\r\n", buf);
    sprintf(buf, "%sContent-Length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-Type: %s\r\n\r\n", buf, filetype);
    printMatrics(buf, req);
    Rio_writen(fd, buf, strlen(buf));

   //  Writes out to the client socket the memory-mapped file 
   Rio_writen(fd, srcp, filesize);
   Munmap(srcp, filesize);
}

// handle a request
void requestHandle(int fd, int *thread_id, struct request_t *req)
{
   int is_static;
   struct stat sbuf;
   char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
   char filename[MAXLINE], cgiargs[MAXLINE];
   rio_t rio;

   Rio_readinitb(&rio, fd);
   Rio_readlineb(&rio, buf, MAXLINE);
   sscanf(buf, "%s %s %s", method, uri, version);

   printf("%s %s %s\n", method, uri, version);

   ++request_counter;

   if (strcasecmp(method, "GET")) {
      requestError(fd, method, "501", "Not Implemented", "OS-HW3 Server does not implement this method");
      return;
   }
   requestReadhdrs(&rio);

   is_static = requestParseURI(uri, filename, cgiargs);
   if (stat(filename, &sbuf) < 0) {
      requestError(fd, filename, "404", "Not found", "OS-HW3 Server could not find this file");
      return;
   }

   if (is_static) {
      if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
         requestError(fd, filename, "403", "Forbidden", "OS-HW3 Server could not read this file");
         return;
      }
      ++static_counter;

       requestServeStatic(fd, filename, sbuf.st_size, req);
   } else {
      if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
         requestError(fd, filename, "403", "Forbidden", "OS-HW3 Server could not run this CGI program");
         return;
      }

      ++dynamic_counter;
       requestServeDynamic(fd, filename, cgiargs, req);
   }
}


int queueIsEmpty() { return CIRCLEQ_EMPTY(&main_req_q); }

int queueIsFull() { return current_size >= max_size; }

void remove_req_by_idx(int to_remove) {
    unsigned int counter = 0;
    struct request_t* iter;
    CIRCLEQ_FOREACH(iter, &main_req_q, pointers){
        if (counter == to_remove){
            CIRCLEQ_REMOVE(&main_req_q, iter, pointers);
            Close(get_req_fd(iter));
            free(iter);
            return;
        }
        counter++;
    }
}
