#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <confuse.h>
#include <string.h>

#include "p2s.h"
#include "s2p.h"

typedef struct {
	pthread_t tid_recv;
	pthread_t tid_send;
} pid_pair_t;

int start_p2s(char *cfg_fname) {
	pid_pair_t pid_pair = {
		.tid_recv = 0,
		.tid_send = 0
	};
	p2s_arg_t args;
	pbuf_t pbuf;
	/* batch_timeout is in milliseconds */
	long buff_size=BUFF_SIZE, send_batch_size=1, batch_timeout=1;
	char *recv_inf = NULL, *saddr = NULL, *sport = NULL;;
	cfg_opt_t opts[] = {
		CFG_SIMPLE_INT("buffer_size", &buff_size),
		CFG_SIMPLE_INT("send_batch_size", &send_batch_size),
		CFG_SIMPLE_INT("batch_timeout", &batch_timeout),
		CFG_SIMPLE_STR("recv_interface", &recv_inf),
		CFG_SIMPLE_STR("server_address", &saddr),
		CFG_SIMPLE_STR("server_port", &sport),
	};
	cfg_t *cfg;
	cfg = cfg_init(opts, 0);
	if (cfg_parse(cfg, cfg_fname) == CFG_FILE_ERROR) {
		fprintf(
			stderr,
			"Configuration file %s cannot open for reading.\n",
			cfg_fname
		);
		return -1;
	}
	if (recv_inf == NULL) {
		fprintf(stderr, "Receive interface name must be specified.\n");
		return -1;
	}
	if (saddr == NULL) {
		fprintf(stderr, "Server address must be specified.\n");
		return -1;
	}
	if (sport == NULL) {
		fprintf(stderr, "Server port must be specified.\n");
		return -1;
	}
	printf("Configuration:\n");
	printf("buffer size: %dB=%dKB\n", (int)buff_size, (int)buff_size/1024);
	printf("send batch size: %dB=%dKB\n", (int)send_batch_size, (int)send_batch_size/1024);
	printf("receive interface: %s\n", recv_inf);
	printf("batch timeout: %dms=%fs\n", (int)batch_timeout, batch_timeout/1000.0);
	printf("server address: %s, server port: %s\n", saddr, sport);
	init_pbuf(&pbuf, buff_size);
	// pbuf.send_batch_size = PKT_MTU;
	args.send_batch_size = send_batch_size;
	args.recv_interface = recv_inf;
	args.ppbuf = &pbuf;
	args.batch_timeout.tv_sec = batch_timeout / 1000;
	args.batch_timeout.tv_nsec = batch_timeout % 1000;
	args.server_addr = saddr;
	args.server_port = sport;
	pthread_create(&pid_pair.tid_recv, NULL, recv_raw_packets, (void*) &args);
	pthread_create(&pid_pair.tid_send, NULL, mptcp_send_data, (void*) &args);
	pthread_join(pid_pair.tid_recv, NULL);
	pthread_join(pid_pair.tid_send, NULL);
	free_pbuf(&pbuf);
	free(recv_inf);
	free(saddr);
	free(sport);
	return 0;
}

int start_s2p(char *cfg_fname) {
	pid_pair_t pid_pair = {
		.tid_recv = 0,
		.tid_send = 0
	};
	s2p_arg_t args;
	ring_buf_t rbuf;
	/* batch_timeout is in milliseconds */
	long buff_size=BUFF_SIZE, recv_batch_size=PKT_MTU+40;
	char *send_inf = NULL, *sport = NULL;;
	cfg_opt_t opts[] = {
		CFG_SIMPLE_INT("buffer_size", &buff_size),
		CFG_SIMPLE_INT("recv_batch_size", &recv_batch_size),
		CFG_SIMPLE_STR("send_interface", &send_inf),
		CFG_SIMPLE_STR("server_port", &sport),
	};
	cfg_t *cfg;
	cfg = cfg_init(opts, 0);
	if (cfg_parse(cfg, cfg_fname) == CFG_FILE_ERROR) {
		fprintf(
			stderr,
			"Configuration file %s cannot open for reading.\n",
			cfg_fname
		);
		return -1;
	}
	if (send_inf == NULL) {
		fprintf(stderr, "Send interface name must be specified.\n");
		return -1;
	}
	if (sport == NULL) {
		fprintf(stderr, "Server port must be specified.\n");
		return -1;
	}
	printf("Configuration:\n");
	printf("buffer size: %dB=%dKB\n", (int)buff_size, (int)buff_size/1024);
	printf("receive batch size: %dB=%dKB\n", (int)recv_batch_size, (int)recv_batch_size/1024);
	printf("send interface: %s\n", send_inf);
	printf("listen port: %s\n", sport);
	init_rbuf(&rbuf, buff_size);
	// pbuf.send_batch_size = PKT_MTU;
	args.recv_batch_size = recv_batch_size;
	args.send_interface = send_inf;
	args.prbuf = &rbuf;
	args.server_port = sport;
	pthread_create(&pid_pair.tid_recv, NULL, mptcp_recv_data, (void*) &args);
	pthread_create(&pid_pair.tid_send, NULL, send_raw_packets, (void*) &args);
	pthread_join(pid_pair.tid_recv, NULL);
	pthread_join(pid_pair.tid_send, NULL);
	free_rbuf(&rbuf);
	free(send_inf);
	free(sport);
	return 0;
}

int main(int argc, char **argv) {
	char cfg_p2s[] = "p2s.cfg";
	char cfg_s2p[] = "s2p.cfg";
	char usage[100];
	sprintf(usage, "%s p2s|s2p", argv[0]);
	if (argc < 2) {
		printf("USAGE: %s\n", usage);
		return 0;
	}
	if (!strcmp(argv[1], "p2s")) {
		start_p2s(cfg_p2s);
	} else if (!strcmp(argv[1], "s2p")) {
		start_s2p(cfg_s2p);
	} else {
		printf("USAGE: %s\n", usage);
	}
	return 0;
}
