#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    /*
    Opening a socket: int socket(int domain, int type, int protocol)
    AF_INET: address family that designates type of addresses socket can communicate with (IPv4 in this case). 
    Have to specify address family before you can use addresses of that type

    SOCK_DGRAM: specifies type of communication of which there are 2 types, stream (TCP, requires prior connection establishment).
    This refers to datagram type - single messages sent without having to establish a connection (UDP)

    Last argument specifies protocol, i.e. TCP/UDP. If 0, chooses based on previous 2 arguments

    Returns 0 on success.
    */
    int sock_des;
    if ((sock_des=socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        return -1;
    }

    /*
    Binding a socket to IP and port: int bind(int socket_des, struct sock_addr* socketName, int addrLength)
    socketName: a sock_addr structure specifying the name of the socket
    addrLength: size of the sock_addr structure specified argument

    typedef struct {
        short sin_family; // communication domain
        u_short sin_port; // bound port to created socket (must be network order)
        struct in_addr sin_addr; // bound IP address to created socket. This struct has only one member, s_addr (must be network order)
        char sin_zero[8]; // padding to match this struct size to general struct size. Initialize to zero
    } sockaddr_in;

    For testing purposes, use Loopback address/localhost:8080 for client, 8000 for server. Must manual bind for specific ports
    Use inet_pton (handles both IPv4 and IPv6) to convert human readable IP to binary network byte-order format
    Server should use INADDR_ANY to listen for any requests/replies.

    htons(): Host to network short, used for 16-bit integers
    htonl(): Host to network long, used for 32-bit integers (IP addresses, 4 bytes)

    bind() returns 0 on success, -1 on error
    */
    // Binding client-side socket
    struct sockaddr_in server_socketAddr;
    server_socketAddr.sin_family = AF_INET;
    server_socketAddr.sin_port = htons(8000);
    server_socketAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    memset(server_socketAddr.sin_zero, '\0', sizeof(server_socketAddr.sin_zero));

    if (bind(sock_des, (struct sockaddr *)&server_socketAddr, sizeof(struct sockaddr_in)) < 0) {
        perror("Bind to socket failed. Closing socket\n");
        close(sock_des);
        return -1;
    }

    // Prepare to receive 1 message from the client. Ordinarily, server would be continuously listening
    /*
    For receiving a message: int recvfrom(int socket_des, char *receiver_buffer, int buffer_len, int flags, struct sockaddr *from, int *from_len)
    The flags argument is normally 0.
    The *from argument RECEIVES the socket address of the sender (for subsequently responding/sending a reply)
    The *fromlen argument specifies the size of the buffer provided for the *from argument.

    recvfrom returns 0 on success, -1 on error
    */
    const int SIZE = 255;
    char message[SIZE];
    struct sockaddr_in client_socketAddr;
    client_socketAddr.sin_family = AF_INET;
    memset(client_socketAddr.sin_zero, '\0', sizeof(client_socketAddr.sin_zero));
    socklen_t len_client_socketAddr = sizeof(client_socketAddr);

    if (recvfrom(sock_des, message, SIZE, 0, (struct sockaddr *)&client_socketAddr, &len_client_socketAddr) < 0) {
        perror("Error receiving message");
        close(sock_des);
        return -1;
    }
    printf("Message received from client: %s", message);

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