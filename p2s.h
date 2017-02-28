#ifndef P2S_H
#define P2S_H

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BUFF_SIZE 100000000
#ifndef PKT_MTU
#define PKT_MTU 1500
#endif
// #define PKT_MTU 10
#define MAX_PKT_SIZE 3000

#define DEBUG 0

#define IPHDR(buff)	((struct iphdr*) (buff))

void *recv_raw_packets(void *argp);
void *mptcp_send_data(void *argp);

typedef struct {
	char *buff; /* (ring) buffer to store packet */
	int head; /* the head packet */
	int n; /* buffer size */
	int len; /* buffer length in Bytes*/
	pthread_mutex_t mutex; /* protect accesses to buf */
	pthread_cond_t cond_send; /* condition to send */
	pthread_cond_t cond_recv; /* condition to receive */
} pbuf_t;

typedef struct {
	pbuf_t *ppbuf;
	char *recv_interface;
	char *server_addr;
	char *server_port;
	struct timespec batch_timeout;
	int send_batch_size; /* batch size to send data */
} p2s_arg_t;

static inline void init_pbuf(pbuf_t *pp, int n) {
	pp->buff = (char*) malloc(sizeof(char) * n);
	pp->buff[0] = '\0';
	pp->n = n;
	pp->len = 0;
	pp->head = 0;
	pthread_mutex_init(&pp->mutex, NULL);
	pthread_cond_init(&pp->cond_send, NULL);
	pthread_cond_init(&pp->cond_recv, NULL);
}

static inline void free_pbuf(pbuf_t *pp) {
	free(pp->buff);
	pp = NULL;
	pthread_cond_destroy(&pp->cond_send);
	pthread_cond_destroy(&pp->cond_recv);
}

static inline int pbuf_avail(pbuf_t *pp) {
	return pp->n - pp->len;
}

static inline int pbuf_tail(pbuf_t *pp) {
	return (pp->head + pp->len) % pp->n;
}

static inline int pbuf_push(pbuf_t *pp, char *src, int n) {
	int tail, i=0;
	if (n > pbuf_avail(pp)) {
		fprintf(stderr, "not enough buffer space to copy into\n");
		fprintf(stderr, "buffer space: %d data length to be copied: %d\n", pbuf_avail(pp), n);
		return 0;
	}
	tail = pbuf_tail(pp);
	for (i = 0; i < n; i++) {
		pp->buff[tail] = src[i];
		tail = (tail + 1) % pp->n;
	}
	pp->len += n;
	return n;
}

static inline int pbuf_pull(pbuf_t *pp, char *dst, int n) {
	int i = 0;
	if (n > pp->len) {
		n = pp->len;
	}
	for (i = 0; i < n; i++) {
		dst[i] = pp->buff[pp->head];
		pp->head = (pp->head + 1) % pp->n;
	}
	pp->len -= n;
	return n;
}

#endif
