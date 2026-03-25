#include <iostream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h> 
#include <vector>
#include <map>
#include <string>
#include <cstdint>

#pragma comment(lib, "ws2_32.lib") 

// 1. Data Structures as per Manual Requirements [cite: 41, 62]
enum Currency { USD = 1, EUR = 2 };

struct BankAccount {
    int accountNumber;
    std::string name;
    std::string password;
    int currency;
    float balance;
};

// Global Storage 
std::map<int, BankAccount> accountDatabase;
std::map<int, std::string> requestHistory; // For At-Most-Once semantics [cite: 105]
int nextAccountNumber = 1000;

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { 
        return -1;
    }

    SOCKET sock_des;
    if ((sock_des = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        WSACleanup();
        return -1;
    }

    // 2. Port configuration for NTU Lab [cite: 116]
    sockaddr_in server_socketAddr{}; 
    server_socketAddr.sin_family = AF_INET;
    server_socketAddr.sin_port = htons(2222); // Required port
    server_socketAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock_des, (struct sockaddr*)&server_socketAddr, sizeof(server_socketAddr)) == SOCKET_ERROR) {
        closesocket(sock_des);
        WSACleanup();
        return -1;
    }

    std::cout << "Server listening on port 2222..." << std::endl;

    const int SIZE = 1024;
    char buffer[SIZE]{}; 
    sockaddr_in client_socketAddr{};
    int len_client_socketAddr = sizeof(client_socketAddr); 
    
    while (true) {
        memset(buffer, 0, SIZE);
        int bytesReceived = recvfrom(sock_des, buffer, SIZE, 0, (struct sockaddr*)&client_socketAddr, &len_client_socketAddr);
        
        if (bytesReceived == SOCKET_ERROR) continue;

        int offset = 0;

        // 3. Manual Unmarshalling (Network Byte Order) [cite: 95, 101]
        uint32_t net_opcode;
        memcpy(&net_opcode, buffer + offset, 4);
        int opcode = ntohl(net_opcode);
        offset += 4;

        uint32_t net_reqId;
        memcpy(&net_reqId, buffer + offset, 4);
        int requestId = ntohl(net_reqId);
        offset += 4;

        // 4. At-Most-Once Check [cite: 104, 108]
        if (requestHistory.count(requestId)) {
            std::cout << "Duplicate request " << requestId << " detected. Re-sending cached reply." << std::endl;
            std::string cachedReply = requestHistory[requestId];
            sendto(sock_des, cachedReply.c_str(), cachedReply.length(), 0, (struct sockaddr*)&client_socketAddr, sizeof(client_socketAddr));
            continue;
        }

        std::string response = "Operation successful";

        // 5. Service Logic [cite: 47, 53, 56]
        switch (opcode) {
            case 1: { // Open Account
                uint32_t nLen;
                memcpy(&nLen, buffer + offset, 4);
                int nameLen = ntohl(nLen);
                offset += 4;
                std::string name(buffer + offset, nameLen);
                offset += nameLen;

                uint32_t pLen;
                memcpy(&pLen, buffer + offset, 4);
                int pwLen = ntohl(pLen);
                offset += 4;
                std::string pw(buffer + offset, pwLen);
                offset += pwLen;

                uint32_t net_curr;
                memcpy(&net_curr, buffer + offset, 4);
                int curr = ntohl(net_curr);
                offset += 4;

                float balance;
                memcpy(&balance, buffer + offset, 4);
                
                BankAccount acc = { ++nextAccountNumber, name, pw, curr, balance };
                accountDatabase[acc.accountNumber] = acc;
                response = "Account Created: " + std::to_string(acc.accountNumber);
                break;
            }

            case 2: { // Close Account
                uint32_t nLen;
                memcpy(&nLen, buffer + offset, 4);
                int nameLen = ntohl(nLen);
                offset += 4;
                std::string name(buffer + offset, nameLen);
                offset += nameLen;

                uint32_t accNumNet;
                memcpy(&accNumNet, buffer + offset, 4);
                int accNum = ntohl(accNumNet);
                offset += 4;

                // Validate account [cite: 55]
                if (accountDatabase.count(accNum) && accountDatabase[accNum].name == name) {
                    accountDatabase.erase(accNum);
                    response = "Account " + std::to_string(accNum) + " closed successfully.";
                } else {
                    response = "Error: Account not found or name mismatch.";
                }
                break;
            }

            case 3: // Deposit
            case 4: { // Withdraw
                uint32_t accNumNet;
                memcpy(&accNumNet, buffer + offset, 4);
                int accNum = ntohl(accNumNet);
                offset += 4;

                uint32_t pLen;
                memcpy(&pLen, buffer + offset, 4);
                int pwLen = ntohl(pLen);
                offset += 4;
                std::string pw(buffer + offset, pwLen);
                offset += pwLen;

                float amount;
                memcpy(&amount, buffer + offset, 4);

                if (accountDatabase.count(accNum) && accountDatabase[accNum].password == pw) {
                    if (opcode == 4 && accountDatabase[accNum].balance < amount) {
                        response = "Error: Insufficient balance.";
                    } else {
                        accountDatabase[accNum].balance += (opcode == 3 ? amount : -amount);
                        response = "New Balance: " + std::to_string(accountDatabase[accNum].balance);
                    }
                } else {
                    response = "Error: Invalid credentials.";
                }
                break;
            }
            
            default:
                response = "Error: Unknown Opcode";
        }

        // Store result for At-Most-Once and reply [cite: 104]
        requestHistory[requestId] = response;
        sendto(sock_des, response.c_str(), response.length(), 0, (struct sockaddr*)&client_socketAddr, sizeof(client_socketAddr));
    }

    closesocket(sock_des);
    WSACleanup(); 
    return 0;
}