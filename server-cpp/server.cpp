#include <iostream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h> 
#include <vector>
#include <map>
#include <string>
#include <cstdint>
#include <chrono>
#include <functional>
#include <thread>
#include <mutex>

#pragma comment(lib, "ws2_32.lib") 

enum Currency: uint32_t { USD = 1, JPY = 2, SGD = 3 };

struct BankAccount {
    uint32_t accountNumber;
    std::string name;
    std::string password;
    Currency currency;
    float balance;
};

struct ClientCallbackDetails {
    uint32_t id;
    struct in_addr client_addr;
    uint16_t client_port;
};

// ==========================================
// BankService: Core Business Logic
// ==========================================
class BankService {
private:
    std::map<uint32_t, BankAccount> accountDatabase;
    uint32_t nextAccountNumber = 0;

public:
    std::string openAccount(const std::string& name, const std::string& pw, Currency curr, float balance) {
        BankAccount acc = { nextAccountNumber++, name, pw, curr, balance };
        accountDatabase[acc.accountNumber] = acc;
        std::cout << "Created account " << acc.accountNumber << " for " << name << std::endl;
        return "Account Created: " + std::to_string(acc.accountNumber);
    }

    std::string closeAccount(const std::string& name, int accNum, const std::string& pw) {
        if (accountDatabase.count(accNum) && accountDatabase[accNum].name == name && accountDatabase[accNum].password == pw) {
            accountDatabase.erase(accNum);
            std::cout << "Closed account " << accNum << std::endl;
            return "Account " + std::to_string(accNum) + " closed successfully.";
        }
        return "Error: Invalid credentials or account not found.";
    }

    std::string deposit(int accNum, const std::string& pw, float amount) {
        if (accountDatabase.count(accNum) && accountDatabase[accNum].password == pw) {
            accountDatabase[accNum].balance += amount;
            std::cout << "Deposited to " << accNum << " | New Balance: " << accountDatabase[accNum].balance << std::endl;
            return "New Balance: " + std::to_string(accountDatabase[accNum].balance);
        }
        return "Error: Invalid credentials.";
    }

    std::string withdraw(int accNum, const std::string& pw, float amount) {
        if (accountDatabase.count(accNum) && accountDatabase[accNum].password == pw) {
            if (accountDatabase[accNum].balance < amount) {
                return "Error: Insufficient balance.";
            }
            accountDatabase[accNum].balance -= amount;
            std::cout << "Withdrew from " << accNum << " | New Balance: " << accountDatabase[accNum].balance << std::endl;
            return "New Balance: " + std::to_string(accountDatabase[accNum].balance);
        }
        return "Error: Invalid credentials.";
    }
};

// ==========================================
// MessageParser: UDP Payload Unmarshalling
// ==========================================
class MessageParser {
private:
    std::map<std::string, std::string> requestHistory;
    std::string semantics;

    std::vector<ClientCallbackDetails> clientsMonitoring;
    uint32_t clientId;
    std::mutex m;

    uint32_t readInt32(const char* buffer, int& offset) {
        uint32_t net_val;
        memcpy(&net_val, buffer + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        return ntohl(net_val);
    }

    float readFloat(const char* buffer, int& offset) {
        uint32_t net_val;
        memcpy(&net_val, buffer + offset, sizeof(uint32_t));
        uint32_t host_val = ntohl(net_val);
        float f_val;
        memcpy(&f_val, &host_val, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        return f_val;
    }

    std::string readString(const char* buffer, int& offset) {
        uint32_t len = readInt32(buffer, offset);
        std::string str(buffer + offset, len);
        offset += len;
        return str;
    }

    int64_t readLong64(const char* buffer, int& offset) {
        uint32_t tmp = readInt32(buffer, offset);
        int64_t net_val = ((uint64_t)(tmp & 0xFFFFFFFF) << 32);
        tmp = readInt32(buffer, offset);
        return net_val | tmp;
    }


public:
    MessageParser (std::string semantics) : semantics(semantics) {
        clientId = 0;
    }
    std::string processMessage(const char* buffer, sockaddr_in& client_socketAddr, BankService& bank) {
        int offset = 0;
        
        int opcode = readInt32(buffer, offset);
        int requestId = readInt32(buffer, offset);

        std::string clientKey = std::string(inet_ntoa(client_socketAddr.sin_addr)) + ":" + 
                                std::to_string(ntohs(client_socketAddr.sin_port)) + "-" + 
                                std::to_string(requestId);

        if (requestHistory.count(clientKey)) {
            std::cout << "Duplicate request " << clientKey << " detected. Re-sending cached reply." << std::endl;
            return requestHistory[clientKey];
        }

        std::string response;

        switch (opcode) {
            case 1: { // Open Account
                std::string name = readString(buffer, offset);
                std::string pw = readString(buffer, offset);
                Currency curr = static_cast<Currency>(readInt32(buffer, offset));
                float balance = readFloat(buffer, offset);
                response = bank.openAccount(name, pw, curr, balance);
                break;
            }
            case 2: { // Close Account
                std::string name = readString(buffer, offset);
                int accNum = readInt32(buffer, offset);
                std::string pw = readString(buffer, offset);
                response = bank.closeAccount(name, accNum, pw);
                break;
            }
            case 3: { // Deposit
                int accNum = readInt32(buffer, offset);
                std::string pw = readString(buffer, offset);
                float amount = readFloat(buffer, offset);
                response = bank.deposit(accNum, pw, amount);
                break;
            }
            case 4: { // Withdraw
                int accNum = readInt32(buffer, offset);
                std::string pw = readString(buffer, offset);
                float amount = readFloat(buffer, offset);
                response = bank.withdraw(accNum, pw, amount);
                break;
            }
            case 5: { // Monitor
                int64_t duration = readLong64(buffer, offset);
                clientsMonitoring.push_back({clientId++, client_socketAddr.sin_addr, ntohs(client_socketAddr.sin_port)});
                // TODO: 
                break;
            }
            default:
                response = "Error: Unknown Opcode " + std::to_string(opcode);
                std::cout << "Received unknown opcode: " << opcode << std::endl;
        }

        if (semantics == "amo")
            requestHistory[clientKey] = response;
        return response;
    }
};

// ==========================================
// UDPServer: Socket Lifecycle Management
// ==========================================
class UDPServer {
private:
    SOCKET sock_des;
    const int SIZE = 1024;

public:
    UDPServer(int port) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { 
            std::cerr << "WSAStartup failed." << std::endl;
            exit(-1);
        }

        if ((sock_des = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
            std::cerr << "Socket creation failed." << std::endl;
            WSACleanup();
            exit(-1);
        }

        sockaddr_in server_socketAddr{}; 
        server_socketAddr.sin_family = AF_INET;
        server_socketAddr.sin_port = htons(port); 
        server_socketAddr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(sock_des, (struct sockaddr*)&server_socketAddr, sizeof(server_socketAddr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
            closesocket(sock_des);
            WSACleanup();
            exit(-1);
        }
        std::cout << "Server listening on port " << port << "..." << std::endl;
    }

    ~UDPServer() {
        closesocket(sock_des);
        WSACleanup(); 
    }

    void start(BankService& bank, MessageParser& parser) {
        char buffer[1024]{}; 
        sockaddr_in client_socketAddr{};
        int len_client_socketAddr = sizeof(client_socketAddr); 
        
        while (true) {
            memset(buffer, 0, SIZE);
            int bytesReceived = recvfrom(sock_des, buffer, SIZE, 0, (struct sockaddr*)&client_socketAddr, &len_client_socketAddr);
            
            if (bytesReceived == SOCKET_ERROR) continue;

            std::string response = parser.processMessage(buffer, client_socketAddr, bank);

            sendto(sock_des, response.c_str(), response.length(), 0, (struct sockaddr*)&client_socketAddr, sizeof(client_socketAddr));
        }
    }
};

// ==========================================
// Main Entry Point
// ==========================================
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Too many or too few arguments. Please specify the invocation semantics as amo (at-most-once) or alo (at-least-once)" << std::endl;
        return -1;
    }
    std::string semantics = argv[1];
    if (semantics != "amo" && semantics != "alo"){
        std::cout << "Please specify the invocation semantics as amo (at-most-once) or alo (at-least-once)" << std::endl;
        return -1;
    }

    BankService bank;
    MessageParser parser(semantics);
    UDPServer server(8080);

    server.start(bank, parser);

    return 0;
}