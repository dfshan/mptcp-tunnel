#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "p2s.h"


int main(int argc, char **argv) {
	if (argc < 2) {
		printf("USAGE: %s <interface>\n", argv[0]);
		return 1;
	}
	pthread_t tid_recv, tid_send;
	pthread_arg_t args;
	pbuf_t pbuf;
	init_pbuf(&pbuf, BUFF_SIZE);
	pbuf.send_batch_size = PKT_MTU;
	args.recv_interface = argv[1];
	args.send_interface = argv[1];
	args.ppbuf = &pbuf;
	args.batch_timeout.tv_sec = 5;
	args.batch_timeout.tv_nsec = 0;
	pthread_create(&tid_recv, NULL, recv_raw_packets, (void*) &args);
	pthread_create(&tid_send, NULL, mptcp_send_data, (void*) &args);
	pthread_join(tid_recv, NULL);
	pthread_join(tid_send, NULL);
	free_pbuf(&pbuf);
	return 0;
}
