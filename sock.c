#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "sock.h"

/********************************
 * Client/server helper functions
 ********************************/
/*
 * open_clientfd - Open connection to server at <hostname, port> and
 *	 return a socket descriptor ready for reading and writing. This
 *	 function is reentrant and protocol-independent.
 *
 *	 On error, returns:
 *	   -2 for getaddrinfo error
 *	   -1 with errno set for other errors.
 */
/* $begin open_clientfd */
int open_clientfd(char *hostname, char *port) {
	int clientfd, rc;
	struct addrinfo hints, *listp, *p;

	/* Get a list of potential server addresses */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_STREAM;  /* Open a connection */
	hints.ai_flags = AI_NUMERICSERV;  /* ... using a numeric port arg. */
	hints.ai_flags |= AI_ADDRCONFIG;  /* Recommended for connections */
	if ((rc = getaddrinfo(hostname, port, &hints, &listp)) != 0) {
		fprintf(stderr, "getaddrinfo failed (%s:%s): %s\n", hostname, port, gai_strerror(rc));
		return -2;
	}

	/* Walk the list for one that we can successfully connect to */
	for (p = listp; p; p = p->ai_next) {
		/* Create a socket descriptor */
		if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
			continue; /* Socket failed, try the next */

		/* Connect to the server */
		if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
			break; /* Success */
		if (close(clientfd) < 0) { /* Connect failed, try another */  //line:netp:openclientfd:closefd
			fprintf(stderr, "open_clientfd: close failed: %s\n", strerror(errno));
			return -1;
		}
	}

	/* Clean up */
	freeaddrinfo(listp);
	if (!p) /* All connects failed */
		return -1;
	else	/* The last connect succeeded */
		return clientfd;
}

int open_listenfd(int port)
{
	int listenfd, optval=1;
	struct sockaddr_in saddr;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		fprintf(stderr, "create socket failed: %s\n", strerror(errno));
		return listenfd;
	}
	/* Eliminates "Address already in use" error from bind */
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
		(const void *)&optval , sizeof(int));

	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = INADDR_ANY;
	saddr.sin_port = htons(port);

	if (bind(listenfd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
		fprintf(stderr, "bind() failed: %s\n", strerror(errno));
		if (close(listenfd) < 0) {
			fprintf(stderr, "open_listenfd close failed: %s\n", strerror(errno));
		}
		return -1;
	}
	if (listen(listenfd, LISTENQ) < 0) {
		close(listenfd);
		return -1;
	}
	return listenfd;
}
/* $end open_listenfd */
