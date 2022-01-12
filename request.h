#ifndef __REQUEST_H__

struct request_t;

void requestHandle(int fd, int *thread_id, struct request_t *req);

int get_req_fd(struct request_t* req);

struct request_queue;

void init_q();

void push_request_queue(int fd, int overload);
struct request_t *pop_request_queue();
int queueIsEmpty();
int queueIsFull();
void remove_req_by_idx(int to_remove);

#endif
