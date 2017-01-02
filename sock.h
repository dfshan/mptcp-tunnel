#ifndef SOCK_H
#define SOCK_H

#define LISTENQ 5

int open_clientfd(char *hostname, char *port);
int open_listenfd(int port);

#endif
