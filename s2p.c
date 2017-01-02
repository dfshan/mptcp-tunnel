#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>

#include "s2p.h"
#include "sock.h"

void *mptcp_recv_data(void *argp) {
	int sockfd, connfd, recv_size, ret;
	socklen_t clientlen;
	struct sockaddr_in client_addr;
	s2p_arg_t* args = (s2p_arg_t *) argp;
	ring_buf_t *prbuf = args->prbuf;
	char *recv_buff = (char *) malloc(sizeof(char) * args->recv_batch_size);
	if (recv_buff == NULL) {
		fprintf(stderr, "unable to alloc memory for recv buffer\n");
		return NULL;
	}
	sockfd = open_listenfd(atoi(args->server_port));
	clientlen = sizeof(client_addr);
	connfd = accept(sockfd, (struct sockaddr*) &client_addr, &clientlen);
	if (connfd < 0) {
		fprintf(stderr, "accept() error: %s\n", strerror(errno));
		goto out;
	}
	printf(
		"Connection from host %s, port %d.\n",
		inet_ntoa(client_addr.sin_addr),
		ntohs(client_addr.sin_port)
	);
	while (1) {
		recv_size = read(connfd, recv_buff, args->recv_batch_size);
		if (recv_size < 0) {
			fprintf(stderr, "ERROR reading from socket: %s\n", strerror(errno));
		} else if (recv_size == 0) {
			break;
		}
		printf("receive %dB data\n", recv_size);
		// copy data to buffer
		pthread_mutex_lock(&prbuf->mutex);
		while (rbuf_avail(prbuf) < recv_size) {
			printf("not enough buffer space\n");
			ret = pthread_cond_wait(&prbuf->cond_recv, &prbuf->mutex);
			if (ret < 0) {
				fprintf(
					stderr, "mptcp_recv_data: Error when calling pthread_cond_wait: %s\n",
					strerror(ret)
				);
				pthread_mutex_unlock(&prbuf->mutex);
				goto out;
			}
		}
		rbuf_push(prbuf, recv_buff, recv_size);
		ret = pthread_cond_signal(&prbuf->cond_send);
		if (ret < 0) {
			fprintf(
				stderr, "recv_raw_data: Error when calling pthread_cond_signal: %s\n",
				strerror(ret)
			);
			pthread_mutex_unlock(&prbuf->mutex);
			goto out;
		}
		pthread_mutex_unlock(&prbuf->mutex);
		usleep(1);
	}
out:
	free(recv_buff);
	close(sockfd);
	return NULL;
}

void *send_raw_packets(void *argp) {
	s2p_arg_t *args = (s2p_arg_t *) argp;
	ring_buf_t *prbuf = args->prbuf;
	char *interface = args->send_interface;
	int ret, sockfd, pkt_len, on = 1, sent_size, i;
	struct iphdr *iph;
	int iph_len = sizeof(struct iphdr);
	struct ifreq ifr_idx, ifr_saddr, ifr_baddr;
	struct sockaddr_in dstaddr;
	char *send_buff = (char*) malloc(sizeof(char) * PKT_MTU);
	if (send_buff == NULL) {
		fprintf(stderr, "unable to alloc memory for send buffer\n");
		return NULL;
	}
	// Submit request for a socket descriptor to look up interface.
	if ((sockfd = socket (AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
		perror ("socket() failed to get socket descriptor for using ioctl() ");
		exit (EXIT_FAILURE);
	}

	// Use ioctl() to look up interface index which we will use to
	// bind socket descriptor sockfd to specified interface with setsockopt() since
	// none of the other arguments of sendto() specify which interface to use.
	memset (&ifr_idx, 0, sizeof (ifr_idx));
	memset (&ifr_saddr, 0, sizeof (ifr_saddr));
	memset (&ifr_baddr, 0, sizeof (ifr_baddr));
	snprintf (ifr_idx.ifr_name, sizeof (ifr_idx.ifr_name), "%s", interface);
	snprintf (ifr_saddr.ifr_name, sizeof (ifr_saddr.ifr_name), "%s", interface);
	snprintf (ifr_baddr.ifr_name, sizeof (ifr_baddr.ifr_name), "%s", interface);
	if (ioctl (sockfd, SIOCGIFINDEX, &ifr_idx) < 0) {
		perror ("ioctl() failed to find interface ");
		return NULL;
	}
	// get ip address of the interface
	if (ioctl (sockfd, SIOCGIFADDR, &ifr_saddr) < 0) {
		perror ("ioctl() failed to get ip address of this interface ");
		goto out;
	}
	// get broadcast address of the interface
	if (ioctl (sockfd, SIOCGIFBRDADDR, &ifr_baddr) < 0) {
		perror ("ioctl() failed to get ip address of this interface ");
		goto out;
	}
	close (sockfd);
	// Submit request for a raw socket descriptor.
	if ((sockfd = socket (AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
		perror ("socket() failed ");
		goto out;
	}

	// Set flag so socket expects us to provide IPv4 header.
	if (setsockopt (sockfd , IPPROTO_IP, IP_HDRINCL, &on, sizeof (on)) < 0) {
		perror ("setsockopt() failed to set IP_HDRINCL ");
		goto out;
	}

	// Bind socket to interface index.
	if (setsockopt (sockfd, SOL_SOCKET, SO_BINDTODEVICE, &ifr_idx, sizeof (ifr_idx)) < 0) {
		perror ("setsockopt() failed to bind to interface ");
		goto out;
	}
	printf ("Index for interface %s is %i\n", interface, ifr_idx.ifr_ifindex);
	while (1) {
		pthread_mutex_lock(&prbuf->mutex);
		// wait to send data until there is enough data in buffer
		while (rbuf_length(prbuf) < iph_len) {
			ret = pthread_cond_wait(&prbuf->cond_send, &prbuf->mutex);
			if (ret < 0) {
				fprintf(
					stderr, "Error when calling pthread_cond_wait: %s\n",
					strerror(ret)
				);
				pthread_mutex_unlock(&prbuf->mutex);
				goto out;
			}
			printf("rbuf length: %d\n", rbuf_length(prbuf));
		}
		printf("begin send\n");
		/* first, try to copy ip header */
		for (i = 0; i < iph_len; i++) {
			send_buff[i] = prbuf->buff[(prbuf->head + i)%prbuf->n];
		}
		iph = IPHDR(send_buff);
		pkt_len = ntohs(iph->tot_len);
		while (rbuf_length(prbuf) < pkt_len) {
			ret = pthread_cond_wait(&prbuf->cond_send, &prbuf->mutex);
			if (ret < 0) {
				fprintf(
					stderr, "Error when calling pthread_cond_wait: %s\n",
					strerror(ret)
				);
				pthread_mutex_unlock(&prbuf->mutex);
				goto out;
			}
		}
		/* copy the packet into send buffer */
		for (i = iph_len; i < pkt_len; i++) {
			send_buff[i] = prbuf->buff[(prbuf->head + i)%prbuf->n];
		}
		prbuf->head = (prbuf->head + pkt_len) % prbuf->n;
		ret = pthread_cond_signal(&prbuf->cond_recv);
		if (ret < 0) {
			fprintf(
				stderr, "send_raw_packets: Error when calling pthread_cond_signal: %s\n",
				strerror(ret)
			);
			pthread_mutex_unlock(&prbuf->mutex);
			goto out;
		}
		pthread_mutex_unlock(&prbuf->mutex);
		// send the data into network
		// check whether the detination is to its self
		if (iph->daddr == ((struct sockaddr_in*) &ifr_saddr.ifr_addr)->sin_addr.s_addr) {
			continue;
		} else if (iph->daddr == ((struct sockaddr_in*) &ifr_baddr.ifr_broadaddr)->sin_addr.s_addr) {
			continue;
		} else if (iph->daddr == 0xffffff) {
			continue;
		}
		dstaddr.sin_family = AF_INET;
		dstaddr.sin_addr.s_addr = iph->daddr;
		sent_size = sendto(
			sockfd, send_buff, pkt_len, 0,
			(struct sockaddr*) &dstaddr, sizeof(struct sockaddr)
		);
		if (sent_size < 0) {
			perror("sendto() failed");
			goto out;
		} else if (sent_size < pkt_len) {
			fprintf(
				stderr,
				"Expect to send %dB data while only send %dB data.\n",
				pkt_len, sent_size
			);
			goto out;
		}
		printf("send %dB data!\n", sent_size);
	}
out:
	close(sockfd);
	free(send_buff);
	return NULL;
}
