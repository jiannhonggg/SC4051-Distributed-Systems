#include <iostream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h> 
#include <vector>
#include <map>
#include <string>
#include <cstdint>

#pragma comment(lib, "ws2_32.lib") 

// 1. Data Structures as per Manual Requirements
enum Currency { USD = 1, JPY = 2, SGD = 3 };

struct BankAccount {
    int accountNumber;
    std::string name;
    std::string password;
    int currency;
    float balance;
};

// Global Storage 
std::map<int, BankAccount> accountDatabase;
std::map<std::string, std::string> requestHistory; // For At-Most-Once semantics
int nextAccountNumber = 1000;

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { 
        std::cerr << "WSAStartup failed." << std::endl;
        return -1;
    }

    SOCKET sock_des;
    if ((sock_des = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        std::cerr << "Socket creation failed." << std::endl;
        WSACleanup();
        return -1;
    }

    // 2. Port configuration for NTU Lab 
    sockaddr_in server_socketAddr{}; 
    server_socketAddr.sin_family = AF_INET;
    server_socketAddr.sin_port = htons(2222); // Required port
    server_socketAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock_des, (struct sockaddr*)&server_socketAddr, sizeof(server_socketAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
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

        // 3. Manual Unmarshalling (Network Byte Order) 
        uint32_t net_opcode;
        memcpy(&net_opcode, buffer + offset, 4);
        int opcode = ntohl(net_opcode);
        offset += 4;

        uint32_t net_reqId;
        memcpy(&net_reqId, buffer + offset, 4);
        int requestId = ntohl(net_reqId);
        offset += 4;
        
        // Unique key for At-Most-Once: IP:PORT-RequestID
        std::string clientKey = std::string(inet_ntoa(client_socketAddr.sin_addr)) + ":" + 
                                std::to_string(ntohs(client_socketAddr.sin_port)) + "-" + 
                                std::to_string(requestId);

        // 4. At-Most-Once Check
        if (requestHistory.count(clientKey)) {
            std::cout << "Duplicate request " << clientKey << " detected. Re-sending cached reply." << std::endl;
            std::string cachedReply = requestHistory[clientKey];
            sendto(sock_des, cachedReply.c_str(), cachedReply.length(), 0, (struct sockaddr*)&client_socketAddr, sizeof(client_socketAddr));
            continue;
        }

        std::string response = "Operation successful";

        // 5. Service Logic
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

                uint32_t net_bal;
                memcpy(&net_bal, buffer + offset, 4);
                uint32_t host_bal = ntohl(net_bal);
                float balance;
                memcpy(&balance, &host_bal, 4);
                offset += 4; 
                
                BankAccount acc = { ++nextAccountNumber, name, pw, curr, balance };
                accountDatabase[acc.accountNumber] = acc;
                response = "Account Created: " + std::to_string(acc.accountNumber);
                std::cout << "Created account " << acc.accountNumber << " for " << name << std::endl;
                break;
            }

            case 2: { // Close Account
                uint32_t net_nLen; memcpy(&net_nLen, buffer + offset, 4);
                uint32_t nLen = ntohl(net_nLen); offset += 4;
                std::string name(buffer + offset, nLen); offset += nLen;

                uint32_t net_acc; memcpy(&net_acc, buffer + offset, 4);
                int accNum = ntohl(net_acc); offset += 4;

                uint32_t net_pLen; memcpy(&net_pLen, buffer + offset, 4);
                uint32_t pLen = ntohl(net_pLen); offset += 4;
                std::string pw(buffer + offset, pLen); offset += pLen;

                if (accountDatabase.count(accNum) && accountDatabase[accNum].name == name && accountDatabase[accNum].password == pw) {
                    accountDatabase.erase(accNum);
                    response = "Account " + std::to_string(accNum) + " closed successfully.";
                    std::cout << "Closed account " << accNum << std::endl;
                } else {
                    response = "Error: Invalid credentials or account not found.";
                }
                break;
            }

            case 3: // Deposit
            case 4: { // Withdraw
                uint32_t net_acc; memcpy(&net_acc, buffer + offset, 4);
                int accNum = ntohl(net_acc); offset += 4;

                uint32_t net_pLen; memcpy(&net_pLen, buffer + offset, 4);
                uint32_t pLen = ntohl(net_pLen); offset += 4;
                std::string pw(buffer + offset, pLen); offset += pLen;

                uint32_t net_amt; memcpy(&net_amt, buffer + offset, 4);
                uint32_t host_amt = ntohl(net_amt);
                float amount; memcpy(&amount, &host_amt, 4);
                offset += 4; 

                if (accountDatabase.count(accNum) && accountDatabase[accNum].password == pw) {
                    if (opcode == 4 && accountDatabase[accNum].balance < amount) {
                        response = "Error: Insufficient balance.";
                    } else {
                        accountDatabase[accNum].balance += (opcode == 3 ? amount : -amount);
                        response = "New Balance: " + std::to_string(accountDatabase[accNum].balance);
                        std::cout << (opcode == 3 ? "Deposited to " : "Withdrew from ") << accNum << std::endl;
                    }
                } else {
                    response = "Error: Invalid credentials.";
                }
                break;
            }
            
            default:
                response = "Error: Unknown Opcode " + std::to_string(opcode);
                std::cout << "Received unknown opcode: " << opcode << std::endl;
        }

        // Store result for At-Most-Once and reply
        requestHistory[clientKey] = response;
        sendto(sock_des, response.c_str(), response.length(), 0, (struct sockaddr*)&client_socketAddr, sizeof(client_socketAddr));
    }

    closesocket(sock_des);
    WSACleanup(); 
    return 0;
}