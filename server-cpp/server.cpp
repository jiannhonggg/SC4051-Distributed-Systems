#include <iostream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h> 

#pragma comment(lib, "ws2_32.lib") 

int main() {
    std::cout << "Server starting..." << std::endl;
    
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { 
        std::cerr << "WSAStartup failed." << std::endl;
        return -1;
    }
    std::cout << "Winsock initialized." << std::endl;

    SOCKET sock_des;
    if ((sock_des = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        std::cerr << "Socket creation failed. Error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return -1;
    }
    std::cout << "Socket created." << std::endl;

    sockaddr_in server_socketAddr{}; 
    server_socketAddr.sin_family = AF_INET;
    server_socketAddr.sin_port = htons(8000);
    server_socketAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(sock_des, reinterpret_cast<sockaddr*>(&server_socketAddr), sizeof(server_socketAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed. Error: " << WSAGetLastError() << std::endl;
        closesocket(sock_des);
        WSACleanup();
        return -1;
    }
    std::cout << "Bound to port 8000. Waiting for messages..." << std::endl;

    const int SIZE = 255;
    char message[SIZE]{}; 
    sockaddr_in client_socketAddr{};
    int len_client_socketAddr = sizeof(client_socketAddr); 
    
    while (true) {
        memset(message, 0, SIZE); // clear buffer each iteration

        if (recvfrom(sock_des, message, SIZE, 0, 
                     reinterpret_cast<sockaddr*>(&client_socketAddr), &len_client_socketAddr) == SOCKET_ERROR) {
            std::cerr << "Error receiving message. Error: " << WSAGetLastError() << std::endl;
            break;
        }
        
        std::cout << "Message received from client: " << message << std::endl;

        const char* reply_message = "Message received. Hello from server!";
        
        if (sendto(sock_des, reply_message, std::strlen(reply_message), 0, 
                   reinterpret_cast<sockaddr*>(&client_socketAddr), sizeof(client_socketAddr)) == SOCKET_ERROR) {
            std::cerr << "Sending reply failed. Error: " << WSAGetLastError() << std::endl;
            break;
        }

        std::cout << "Reply sent." << std::endl;
    }

    // Cleanup is now OUTSIDE the loop
    closesocket(sock_des);
    WSACleanup(); 
    return 0;
}