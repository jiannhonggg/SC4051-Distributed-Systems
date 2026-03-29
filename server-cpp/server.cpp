#include <iostream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h> 
#include <vector>
#include <map>
#include <string>
#include <cstdint>
#include <chrono>
#include <format>
#include <sstream>
#include <regex>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

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
    std::chrono::steady_clock::time_point expiry;

    // overload > operator
    bool operator > (const ClientCallbackDetails& other) const {
        return expiry > other.expiry;
    }
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
        std::printf("Unmarshalled details: name: %s, password: %s, currency: %d, balance: %.2f", name.c_str(), pw.c_str(), curr, balance);
        //for 
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
            std::cout << "Deposited into account" << accNum << "an amount of " << amount << " | New Balance: " << accountDatabase[accNum].balance << std::endl;
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
            std::cout << "Withdrew from account " << accNum << "an amount of " << amount << " | New Balance: " << accountDatabase[accNum].balance << std::endl;
            return "New Balance: " + std::to_string(accountDatabase[accNum].balance);
        }
        return "Error: Invalid credentials.";
    }
};

// ==========================================
// IterablePriorityQueue
// ==========================================
template <typename T, typename Container = std::vector<T>, typename Compare = std::less<T>>
class IterablePriorityQueue : public std::priority_queue<T, Container, Compare> {
    public:
        Container& internal_vector_form() {
            return this->c;
        }
};

// ==========================================
// MessageParser: UDP Payload Unmarshalling
// ==========================================
class MessageParser {
private:
    std::map<std::string, std::string> requestHistory;
    std::string semantics;

    IterablePriorityQueue<ClientCallbackDetails, std::vector<ClientCallbackDetails>, std::greater<ClientCallbackDetails>> clientsMonitoring;
    uint32_t clientId;
    std::mutex m;
    std::condition_variable client_added;
    bool running;
    std::thread cleaner_thread;

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
    MessageParser (std::string semantics) : semantics(semantics), running(true), clientId(0) {
        cleaner_thread = std::thread(&MessageParser::delete_expired_client_record, this);
    }
    ~MessageParser() {
        std::lock_guard<std::mutex> lock(m);
        running = false;
        client_added.notify_all();
        cleaner_thread.join();
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
                std::cout << "duration: " << duration << std::endl;
                auto expiry = std::chrono::steady_clock::now() + std::chrono::nanoseconds(duration);
                m.lock();
                clientsMonitoring.push({clientId++, client_socketAddr.sin_addr, ntohs(client_socketAddr.sin_port), expiry});
                for (const auto& item : clientsMonitoring.internal_vector_form()) {
                    std::cout << "Testing: " << item.id << ": " << item.client_port << std::endl;
                }
                std::cout << "------" << std::endl;
                m.unlock();
                client_added.notify_one(); // wake thread
                response = std::string("ACK: Server will send updates for ") + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::nanoseconds(duration)).count()) + std::string(" seconds");
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

    void delete_expired_client_record() {
        std::cout << "Thread created" << std::endl;
        std::unique_lock<std::mutex> lock(m);
        while (running) {
            if (clientsMonitoring.empty()) {
                client_added.wait(lock, [this] { return !clientsMonitoring.empty() || !running; });
            } else {
                auto nextExpiry = clientsMonitoring.top().expiry;
                if (nextExpiry <= std::chrono::steady_clock::now()) {
                    clientsMonitoring.pop();
                    continue;
                }
                bool status = client_added.wait_until(lock, nextExpiry, [this, &nextExpiry] {
                    return !running || (!clientsMonitoring.empty() && clientsMonitoring.top().expiry < nextExpiry);
                }); // lock reacquired on notification, predicate true, or on next expiry
                if (status) // predicate evaluates to true
                    continue;
                else { // nextExpiry has elapsed, pop
                    clientsMonitoring.pop();
                }
            }
        }
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
            std::cout << "Produced response: " << response << std::endl;

            sendto(sock_des, response.c_str(), response.length(), 0, (struct sockaddr*)&client_socketAddr, sizeof(client_socketAddr));
        }
    }
};

// ==========================================
// Main Entry Point
// ==========================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Too many or too few arguments. Please specify the invocation semantics as amo (at-most-once) or alo (at-least-once)" << std::endl;
        return -1;
    }
    std::string semantics = argv[1];
    if (semantics != "amo" && semantics != "alo"){
        std::cout << "Please specify the invocation semantics as amo (at-most-once) or alo (at-least-once)" << std::endl;
        return -1;
    }
    // TODO: add in optional argument for server port
    std::ostringstream oss;
    for (size_t i = 1; i < argc; i++) {
        oss << argv[i];
        if (i + 1 < argc)
            oss << " ";
    }
    std::string options = oss.str();
    std::regex r(R"(--server_port=(\d+))");
    std::smatch m;
    int port = 8080;
    if (std::regex_search(options, m, r)) {
        port = stoi(m[1].str());
    }

    BankService bank;
    MessageParser parser(semantics);
    UDPServer server(port);

    server.start(bank, parser);

    return 0;
}