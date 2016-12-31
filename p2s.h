#ifndef P2S_H
#define P2S_H

#include <pthread.h>
#include <stdlib.h>

#define BUFF_SIZE 100000000
 #define PKT_MTU 1512
// #define PKT_MTU 10
#define MAX_PKT_SIZE 10240

#define IPHDR(buff)	((struct iphdr*) (buff))

void *recv_raw_packets(void *argp);
void *mptcp_send_data(void *argp);

typedef struct {
	char *buff; /* Buffer to store packet */
	int n; /* buffer size */
	int len; /* buffer length in Bytes*/
	int send_batch_size; /* batch size to send data */
	pthread_mutex_t mutex; /* protect accesses to buf */
	pthread_cond_t cond_send; /* condition to send */
	pthread_cond_t cond_recv; /* condition to receive */
} pbuf_t;

typedef struct {
	pbuf_t *ppbuf;
	char *recv_interface;
	char *send_interface;
	struct timespec batch_timeout;
} pthread_arg_t;

static inline void init_pbuf(pbuf_t *pp, int n) {
	pp->buff = (char*) malloc(sizeof(char) * n);
	pp->buff[0] = '\0';
	pp->n = n;
	pp->len = 0;
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

#endif
