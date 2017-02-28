#ifndef S2P_H
#define S2P_H

#include <pthread.h>
#include <stdlib.h>

#define BUFF_SIZE 100000000
#ifndef PKT_MTU
#define PKT_MTU 1512
#endif
// #define PKT_MTU 10
#define DEBUG 0

#define IPHDR(buff)	((struct iphdr*) (buff))

void *mptcp_recv_data(void *argp);
void *send_raw_packets(void *argp);

typedef struct {
	char *buff; /* Buffer to store packet */
	int n; /* buffer size */
	int head; /* Reading starts here */
	int tail; /* Writing starts here */
	pthread_mutex_t mutex; /* protect accesses to buf */
	pthread_cond_t cond_send; /* condition to send */
	pthread_cond_t cond_recv; /* condition to receive */
} ring_buf_t;

typedef struct {
	ring_buf_t* prbuf;
	char *send_interface;
	char *server_port;
	int recv_batch_size; /* batch size to send data */
} s2p_arg_t;

static inline void init_rbuf(ring_buf_t *pr, int n) {
	pr->buff = (char*) malloc(sizeof(char) * n);
	pr->buff[0] = '\0';
	pr->n = n;
	pr->head = pr->tail = 0;
	pthread_mutex_init(&pr->mutex, NULL);
	pthread_cond_init(&pr->cond_send, NULL);
	pthread_cond_init(&pr->cond_recv, NULL);
}

static inline void free_rbuf(ring_buf_t *pr) {
	free(pr->buff);
	pr = NULL;
	pthread_cond_destroy(&pr->cond_send);
	pthread_cond_destroy(&pr->cond_recv);
}

static inline int rbuf_length(ring_buf_t *pr) {
	return (pr->tail - pr->head + pr->n) % pr->n;
}

static inline int rbuf_avail(ring_buf_t *pr) {
	return pr->n - 1 - rbuf_length(pr);
}

static inline int rbuf_push(ring_buf_t *pr, char *src, int len) {
	int i = 0;
	if (len > rbuf_avail(pr)) {
		return -1;
	}
	while (i < len) {
		pr->buff[pr->tail] = src[i];
		pr->tail = (pr->tail + 1) % pr->n;
		i++;
	}
	return len;
}

static inline int rbuf_pull(ring_buf_t *pr, char *dst, int len) {
	int i = 0;
	if (len > rbuf_length(pr)) {
		return -1;
	}
	while (i < len) {
		dst[i] = pr->buff[pr->head];
		pr->head = (pr->head + 1) % pr->n;
		i++;
	}
	return len;
}

#endif
