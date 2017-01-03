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

#include "p2s.h"
#include "sock.h"

void *recv_raw_packets(void *argp) {
	int sockfd, ret;
	struct ifreq ifr_idx, ifr_maddr, ifr_saddr, ifr_baddr;
	ssize_t frame_len;
	unsigned ip_pkt_len;
	int sockopt = 1;
	p2s_arg_t *args = (p2s_arg_t *) argp;
	pbuf_t *ppbuf = args->ppbuf;
	unsigned char *macaddr;
	char *interface = args->recv_interface;
	char recv_buff[MAX_PKT_SIZE];
	struct iphdr *iph;
	struct ethhdr *ethh;

	// Submit request for a socket descriptor to look up interface.
	sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
	if (sockfd < 0) {
		perror ("socket() failed to get socket descriptor for using ioctl() ");
		exit(EXIT_FAILURE);
	}
	memset (&ifr_idx, 0, sizeof (ifr_idx));
	memset (&ifr_maddr, 0, sizeof (ifr_maddr));
	memset (&ifr_saddr, 0, sizeof (ifr_saddr));
	memset (&ifr_baddr, 0, sizeof (ifr_baddr));
	snprintf (ifr_idx.ifr_name, sizeof (ifr_idx.ifr_name), "%s", interface);
	snprintf (ifr_maddr.ifr_name, sizeof (ifr_maddr.ifr_name), "%s", interface);
	snprintf (ifr_saddr.ifr_name, sizeof (ifr_saddr.ifr_name), "%s", interface);
	snprintf (ifr_baddr.ifr_name, sizeof (ifr_baddr.ifr_name), "%s", interface);
	// Use ioctl() to look up interface index which we will use to
	// bind socket descriptor sockfd to specified interface with setsockopt() since
	// none of the other arguments of sendto() specify which interface to use.
	if (ioctl (sockfd, SIOCGIFINDEX, &ifr_idx) < 0) {
		perror ("ioctl() failed to find interface ");
		goto out;
	}
	// get mac address of the interface
	if (ioctl (sockfd, SIOCGIFHWADDR, &ifr_maddr) < 0) {
		perror ("ioctl() failed to get mac address of this interface ");
		goto out;
	}
	// get ip address of the interface
	if (ioctl (sockfd, SIOCGIFADDR, &ifr_saddr) < 0) {
		perror ("ioctl() failed to get ip address of this interface ");
		goto out;
	}
	// get broadcast address of the interface
	if (ioctl (sockfd, SIOCGIFBRDADDR, &ifr_baddr) < 0) {
		perror ("ioctl() failed to get broadcast address of this interface ");
		goto out;
	}
	close (sockfd);

	// ETH_P_IP means that we only need ip packet
	sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
	if (sockfd < 0) {
		perror("cannot create socket");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt)) < 0) {
		perror("cannot setsockopt REUSEADDR");
		goto out;
	}

	// Bind socket to interface index.
	if (setsockopt (sockfd, SOL_SOCKET, SO_BINDTODEVICE, &ifr_idx, sizeof (ifr_idx)) < 0) {
		perror ("setsockopt() failed to bind to interface ");
		exit (EXIT_FAILURE);
	}

	macaddr = (unsigned char *) ifr_maddr.ifr_hwaddr.sa_data;
	printf(
		"Mac : %x:%x:%x:%x:%x:%x\n",
		macaddr[0], macaddr[1], macaddr[2],
		macaddr[3], macaddr[4], macaddr[5]
	);

	while (1) {
		pthread_mutex_lock(&ppbuf->mutex);
		// check whether there is engough buffer space
		if (pbuf_avail(ppbuf) < PKT_MTU+50) {
			printf("not enough buffer space\n");
			ret = pthread_cond_wait(&ppbuf->cond_recv, &ppbuf->mutex);
			if (ret < 0) {
				fprintf(
					stderr, "Error when calling pthread_cond_wait: %s\n",
					strerror(ret)
				);
				pthread_mutex_unlock(&ppbuf->mutex);
				goto out;
			}
		}
		pthread_mutex_unlock(&ppbuf->mutex);
		frame_len = recv(sockfd, recv_buff, sizeof(recv_buff), 0);
		if (frame_len < 0) {
			perror("Receive packet.");
			goto out;
		} else if (frame_len == 0) {
			continue;
		} else if (frame_len < ETH_HLEN) {
			fprintf(
				stderr,
				"Length of received packet (%ldB) less than eth header.\n",
				frame_len
			);
			goto out;
		} else if (frame_len < ETH_HLEN + sizeof(struct iphdr)) {
			fprintf(
				stderr,
				"Length of received packet (%ldB) less than ip header.\n",
				frame_len
			);
			goto out;
		}
		ethh = (struct ethhdr*) recv_buff;
		/* skip those packet that are not detined to me */
		if (ethh->h_dest[0] != macaddr[0] || ethh->h_dest[1] != macaddr[1] ||
			ethh->h_dest[2] != macaddr[2] || ethh->h_dest[3] != macaddr[3] ||
			ethh->h_dest[4] != macaddr[4] || ethh->h_dest[5] != macaddr[5]) {
			// printf("Packet is not detined to me, skip it.\n");
			continue;
		}
		iph = IPHDR(recv_buff + ETH_HLEN);
		// check whether the detination is to its self
		if (iph->daddr == ((struct sockaddr_in*) &ifr_saddr.ifr_addr)->sin_addr.s_addr) {
			continue;
		} else if (iph->daddr == ((struct sockaddr_in*) &ifr_baddr.ifr_broadaddr)->sin_addr.s_addr) {
			continue;
		} else if (iph->daddr == 0xffffffff) {
			continue;
		} else if (iph->daddr == 0x0100007f) {
			continue;
		}
		printf("%x\n", iph->daddr);
		ip_pkt_len = ntohs(iph->tot_len);
		if (ip_pkt_len < frame_len - ETH_HLEN) {
			fprintf(
				stderr, "Packet Lost. Capture %lu while packet size is %u\n",
				frame_len - ETH_HLEN, ip_pkt_len
			);
			continue;
		}
		printf("recv: receive %uB data\n", ip_pkt_len);
		pthread_mutex_lock(&ppbuf->mutex);
		memcpy(ppbuf->buff + ppbuf->len, recv_buff + ETH_HLEN, ip_pkt_len);
		/*ip_pkt_len = 1;
		ppbuf->buff[ppbuf->len] = ppbuf->len;
		printf("recv: receive data %u\n", (unsigned char) ppbuf->buff[ppbuf->len]);*/
		ppbuf->len = ppbuf->len + ip_pkt_len;
		if (ppbuf->len >= args->send_batch_size) {
			ret = pthread_cond_signal(&ppbuf->cond_send);
			if (ret < 0) {
				fprintf(
					stderr, "recv_raw_packets: Error when calling pthread_cond_signal: %s\n",
					strerror(ret)
				);
				pthread_mutex_unlock(&ppbuf->mutex);
				goto out;
			}
		}
		pthread_mutex_unlock(&ppbuf->mutex);
		usleep(1);
	}
out:
	close(sockfd);
	return NULL;
}

void *mptcp_send_data(void *argp) {
	p2s_arg_t *args = (p2s_arg_t *) argp;
	pbuf_t *ppbuf = args->ppbuf;
	struct timespec timeout, ctime, intvl = args->batch_timeout;
	char *dstaddr = args->server_addr;
	char *dstport = args->server_port;
	int ret, sockfd, sent_out, left_size, total_size;
	char *send_buff = (char*) malloc(sizeof(char) * ppbuf->n);
	if (send_buff == NULL) {
		fprintf(stderr, "unable to alloc memory for send buffer\n");
		return NULL;
	}
	// Submit request for a raw socket descriptor.
	if ((sockfd = open_clientfd(dstaddr, dstport)) < 0) {
		fprintf(stderr, "open clientfd error: %s\n", strerror(errno));
		exit (EXIT_FAILURE);
	}
	while (1) {
		pthread_mutex_lock(&ppbuf->mutex);
		// Not send packet until the received data size reaches the batch size
		while (ppbuf->len < args->send_batch_size) {
			clock_gettime(CLOCK_REALTIME, &ctime);
			timeout.tv_sec = ctime.tv_sec + intvl.tv_sec;
			timeout.tv_nsec = ctime.tv_nsec + intvl.tv_nsec;
			ret = pthread_cond_timedwait(&ppbuf->cond_send, &ppbuf->mutex, &timeout);
			if (ret == 0) {
				//printf("we can now send\n");
				break;
			} else if (ret == ETIMEDOUT) {
				//printf("TIMEOUT\n");
				break;
			} else {
				fprintf(
					stderr, "Error when calling pthread_cond_timewait: %s\n",
					strerror(ret)
				);
				pthread_mutex_unlock(&ppbuf->mutex);
				goto out;
			}
		}
		/* copy data to send buffer */
		memcpy(send_buff, ppbuf->buff, ppbuf->len);
		left_size = total_size = ppbuf->len;
		ppbuf->len = 0;
		ret = pthread_cond_signal(&ppbuf->cond_recv);
		if (ret < 0) {
			fprintf(
				stderr, "mptcp_send_data: Error when calling pthread_cond_signal: %s\n",
				strerror(ret)
			);
			pthread_mutex_unlock(&ppbuf->mutex);
			goto out;
		}
		pthread_mutex_unlock(&ppbuf->mutex);
		// send the data into network
		while (left_size > 0) {
			sent_out = send(sockfd, send_buff, left_size, 0);
			if (sent_out < 0) {
				perror("send() failed");
				goto out;
			}
			printf("send %dB data!\n", sent_out);
			left_size -= sent_out;
		}
	}
out:
	close (sockfd);
	free(send_buff);
	return NULL;
}
