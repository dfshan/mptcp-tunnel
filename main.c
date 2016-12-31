#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "p2s.h"


int main(int argc, char **argv) {
	if (argc < 2) {
		printf("USAGE: %s <interface>\n", argv[0]);
		return 1;
	}
	
	char server_addr[20] = "192.168.1.1";
	char server_port[5] = "80";
	pthread_t tid_recv, tid_send;
	p2s_arg_t args;
	pbuf_t pbuf;
	init_pbuf(&pbuf, BUFF_SIZE);
	// pbuf.send_batch_size = PKT_MTU;
	pbuf.send_batch_size = 1;
	args.recv_interface = argv[1];
	args.send_interface = argv[1];
	args.ppbuf = &pbuf;
	args.batch_timeout.tv_sec = 5;
	args.batch_timeout.tv_nsec = 0;
	args.server_addr = server_addr;
	args.server_port = server_port;
	pthread_create(&tid_recv, NULL, recv_raw_packets, (void*) &args);
	pthread_create(&tid_send, NULL, mptcp_send_data, (void*) &args);
	pthread_join(tid_recv, NULL);
	pthread_join(tid_send, NULL);
	free_pbuf(&pbuf);
	return 0;
}
