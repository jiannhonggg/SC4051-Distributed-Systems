#include "server_socket.h"
#include <string.h>

int main(void) {

    int sock_fd;
    if (sock_fd == -1) return -1;

    // TODO: server should receive messages and echo each received message
    int bytes_read;
    unsigned char request[MAX_BUFFER_SIZE];
    while (1) {
        // listen for a request
        bytes_read = wait_recv_request(request, sock_fd);

        // server should then parse the request
        /*
        Possible design: 
        */
    }

    // Send reply back to the client
    /*
    Sending a message: int sendto(int sock_des, char *msg, int len, int flags, struct sockaddr *to, int tolen)
    msg: supplies the message and len supplies the number of bytes in the message
    flags: normally zero
    The following flags are available: 1. MSG_OOB (sends out-of-band data on the socket), 2. MSG_DONTROUTE: only for diagnostic or routing programs
    *to: specifies socket address of the receiver, with tolen specifying the size

    sendto returns 0 on success, -1 on error
    */
    char reply_message[] = "Message received. Hello from server!";
    if (sendto(sock_des, reply_message, strlen(reply_message), 0, (struct sockaddr *)&client_socketAddr, sizeof(client_socketAddr)) < 0) {
        perror("Sending reply has failed");
        close(sock_des);
        return -1;
    }
    // close a socket
    return 0;
}