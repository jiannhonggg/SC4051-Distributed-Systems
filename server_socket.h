#ifndef SERVER_SOCKET_H
#define SERVER_SOCKET_H

int create_boundSocket(void);
int wait_recv_request(unsigned char *buffer, int socket_fd);

#endif
