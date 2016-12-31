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

int count = 0;

void *recv_raw_packets(void *argp) {
	int sockfd;
	struct ifreq ifr_idx, ifr_saddr, ifr_baddr;
	ssize_t frame_len;
	unsigned ip_pkt_len;
	int sockopt = 1;
	p2s_arg_t *args = (p2s_arg_t *) argp;
	pbuf_t *ppbuf = args->ppbuf;
	char *interface = args->recv_interface;
	char recv_buff[MAX_PKT_SIZE];
	struct iphdr *iph;

	// Submit request for a socket descriptor to look up interface.
	sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
	if (sockfd < 0) {
		perror ("socket() failed to get socket descriptor for using ioctl() ");
		exit(EXIT_FAILURE);
	}
	memset (&ifr_idx, 0, sizeof (ifr_idx));
	memset (&ifr_saddr, 0, sizeof (ifr_saddr));
	memset (&ifr_baddr, 0, sizeof (ifr_baddr));
	snprintf (ifr_idx.ifr_name, sizeof (ifr_idx.ifr_name), "%s", interface);
	snprintf (ifr_saddr.ifr_name, sizeof (ifr_saddr.ifr_name), "%s", interface);
	snprintf (ifr_baddr.ifr_name, sizeof (ifr_baddr.ifr_name), "%s", interface);
	// Use ioctl() to look up interface index which we will use to
	// bind socket descriptor sockfd to specified interface with setsockopt() since
	// none of the other arguments of sendto() specify which interface to use.
	if (ioctl (sockfd, SIOCGIFINDEX, &ifr_idx) < 0) {
	  perror ("ioctl() failed to find interface ");
	  exit(0);
	}
	// get ip address of the interface
	if (ioctl (sockfd, SIOCGIFADDR, &ifr_saddr) < 0) {
	  perror ("ioctl() failed to get ip address of this interface ");
	  exit(0);
	}
	// get broadcast address of the interface
	if (ioctl (sockfd, SIOCGIFBRDADDR, &ifr_baddr) < 0) {
	  perror ("ioctl() failed to get ip address of this interface ");
	  exit(0);
	}
	close (sockfd);

	// ETH_P_IP means that we only need ip packet
	sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
	if (sockfd < 0) {
		perror("create socket");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt)) < 0) {
		perror("setsockopt REUSEADDR");
		close(sockfd);
		exit(0);
	}

	// Bind socket to interface index.
	if (setsockopt (sockfd, SOL_SOCKET, SO_BINDTODEVICE, &ifr_idx, sizeof (ifr_idx)) < 0) {
		perror ("setsockopt() failed to bind to interface ");
		exit (EXIT_FAILURE);
	}


	while (1) {
		pthread_mutex_lock(&ppbuf->mutex);
		// check whether there is engough buffer space
		if (pbuf_avail(ppbuf) < MAX_PKT_SIZE) {
			printf("not enough buffer space\n");
			pthread_cond_wait(&ppbuf->cond_recv, &ppbuf->mutex);
		}
		pthread_mutex_unlock(&ppbuf->mutex);
		frame_len = recv(sockfd, recv_buff, sizeof(recv_buff), 0);
		if (frame_len < 0) {
			perror("Receive packet.");
			close(sockfd);
			return NULL;
		} else if (frame_len == 0) {
			continue;
		}
		iph = IPHDR(recv_buff + ETH_HLEN);
		printf("%x\n", iph->daddr);
		// check whether the detination is to its self
		if (iph->daddr == ((struct sockaddr_in*) &ifr_saddr.ifr_addr)->sin_addr.s_addr) {
			continue;
		} else if (iph->daddr == ((struct sockaddr_in*) &ifr_baddr.ifr_broadaddr)->sin_addr.s_addr) {
			continue;
		} else if (iph->daddr == 0xffffff) {
			continue;
		}
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
		if (ppbuf->len >= ppbuf->send_batch_size) {
			pthread_cond_signal(&ppbuf->cond_send);
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
	char *interface = args->send_interface;
	struct timespec timeout, ctime, intvl = args->batch_timeout;
	char *dstaddr = args->server_addr;
	char *dstport = args->server_port;
	int ret, sockfd, sent_out, left_size, total_size;
	struct ifreq ifr;
	char *send_buff = NULL;
	// Submit request for a socket descriptor to look up interface.
	if ((sockfd = socket (AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
		perror ("socket() failed to get socket descriptor for using ioctl() ");
		exit (EXIT_FAILURE);
	}
	// Use ioctl() to look up interface index which we will use to
	// bind socket descriptor sockfd to specified interface with setsockopt() since
	// none of the other arguments of sendto() specify which interface to use.
	memset (&ifr, 0, sizeof (ifr));
	snprintf (ifr.ifr_name, sizeof (ifr.ifr_name), "%s", interface);
	if (ioctl (sockfd, SIOCGIFINDEX, &ifr) < 0) {
		perror ("ioctl() failed to find interface ");
		return NULL;
	}
	close (sockfd);
	// Submit request for a raw socket descriptor.
	if ((sockfd = open_clientfd(dstaddr, dstport)) < 0) {
		fprintf(stderr, "open clientfd error: %s\n", strerror(errno));
		exit (EXIT_FAILURE);
	}

	// Bind socket to interface index.
	if (setsockopt (sockfd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof (ifr)) < 0) {
		perror ("setsockopt() failed to bind to interface ");
		exit (EXIT_FAILURE);
	}
	printf ("Index for interface %s is %i\n", interface, ifr.ifr_ifindex);
	while (1) {
		send_buff = (char*) malloc(sizeof(char) * ppbuf->n);
		pthread_mutex_lock(&ppbuf->mutex);
		// Not send packet until the received data size reaches the batch size
		while (ppbuf->len < ppbuf->send_batch_size) {
			clock_gettime(CLOCK_REALTIME, &ctime);
			timeout.tv_sec = ctime.tv_sec + intvl.tv_sec;
			timeout.tv_nsec = ctime.tv_nsec + intvl.tv_nsec;
			ret = pthread_cond_timedwait(&ppbuf->cond_send, &ppbuf->mutex, &timeout);
			if (ret == 0) {
				printf("we can now send\n");
				break;
			} else if (ret == ETIMEDOUT) {
				printf("TIMEOUT\n");
				break;
			} else if (ret == EINVAL) {
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
		pthread_cond_signal(&ppbuf->cond_recv);
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

void *mptcp_send_data_bak(void *argp) {
	p2s_arg_t *args = (p2s_arg_t *) argp;
	pbuf_t *ppbuf = args->ppbuf;
	char *interface = args->send_interface;
	struct timespec timeout, ctime, intvl = args->batch_timeout;
	int ret, sockfd, idx, buflen, on = 1, sent_size;
	struct iphdr *iph;
	struct ifreq ifr_idx, ifr_saddr, ifr_baddr;
	struct sockaddr_in dstaddr;
	char *send_buff = NULL;
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
	  exit(0);
	}
	// get broadcast address of the interface
	if (ioctl (sockfd, SIOCGIFBRDADDR, &ifr_baddr) < 0) {
	  perror ("ioctl() failed to get ip address of this interface ");
	  exit(0);
	}
	close (sockfd);
	// Submit request for a raw socket descriptor.
	if ((sockfd = socket (AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
		perror ("socket() failed ");
		exit (EXIT_FAILURE);
	}

	// Set flag so socket expects us to provide IPv4 header.
	if (setsockopt (sockfd , IPPROTO_IP, IP_HDRINCL, &on, sizeof (on)) < 0) {
		perror ("setsockopt() failed to set IP_HDRINCL ");
		exit (EXIT_FAILURE);
	}

	// Bind socket to interface index.
	if (setsockopt (sockfd, SOL_SOCKET, SO_BINDTODEVICE, &ifr_idx, sizeof (ifr_idx)) < 0) {
		perror ("setsockopt() failed to bind to interface ");
		exit (EXIT_FAILURE);
	}
	printf ("Index for interface %s is %i\n", interface, ifr_idx.ifr_ifindex);
	while (1) {
		send_buff = (char*) malloc(sizeof(char) * ppbuf->n);
		pthread_mutex_lock(&ppbuf->mutex);
		// Not send packet until the received data size reaches the batch size
		while (ppbuf->len < ppbuf->send_batch_size) {
			clock_gettime(CLOCK_REALTIME, &ctime);
			timeout.tv_sec = ctime.tv_sec + intvl.tv_sec;
			timeout.tv_nsec = ctime.tv_nsec + intvl.tv_nsec;
			ret = pthread_cond_timedwait(&ppbuf->cond_send, &ppbuf->mutex, &timeout);
			if (ret == 0) {
				printf("we can now send\n");
				break;
			} else if (ret == ETIMEDOUT) {
				printf("TIMEOUT\n");
				break;
			} else if (ret == EINVAL) {
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
		buflen = ppbuf->len;
		ppbuf->len = 0;
		pthread_cond_signal(&ppbuf->cond_recv);
		pthread_mutex_unlock(&ppbuf->mutex);
		idx = 0;
		// send the data into network
		while (idx < buflen) {
			iph = IPHDR(send_buff);
			// check whether the detination is to its self
			if (iph->daddr == ((struct sockaddr_in*) &ifr_saddr.ifr_addr)->sin_addr.s_addr) {
				idx += ntohs(iph->tot_len);
				continue;
			} else if (iph->daddr == ((struct sockaddr_in*) &ifr_baddr.ifr_broadaddr)->sin_addr.s_addr) {
				idx += ntohs(iph->tot_len);
				continue;
			} else if (iph->daddr == 0xffffff) {
				idx += ntohs(iph->tot_len);
				continue;
			}
			dstaddr.sin_family = AF_INET;
			dstaddr.sin_addr.s_addr = iph->saddr;
			iph->saddr = iph->daddr;
			iph->daddr = dstaddr.sin_addr.s_addr;
			sent_size = sendto(
				sockfd, send_buff, ntohs(iph->tot_len), 0,
				(struct sockaddr*) &dstaddr, sizeof(struct sockaddr)
			);
			if (sent_size < 0) {
				perror("sendto() failed");
				goto out;
			}
			printf("send %dB data!\n", sent_size);
			idx += ntohs(iph->tot_len);
		}
	}
out:
	close (sockfd);
	free(send_buff);
	return NULL;
}
