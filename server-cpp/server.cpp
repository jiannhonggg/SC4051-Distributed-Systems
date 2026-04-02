#include <iostream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h> 
#include <vector>
#include <map>
#include <string>
#include <cstdint>
#include <chrono>

#include <sstream>
#include <regex>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <iomanip>
#include <random>

#pragma comment(lib, "ws2_32.lib") 

enum Currency: uint32_t { USD = 1, JPY = 2, SGD = 3 };
std::map <Currency, std::string> CurrToStr = {
    {Currency::USD, "USD"},
    {Currency::JPY, "JPY"},
    {Currency::SGD, "SGD"}
};

struct BankAccount {
    uint32_t accountNumber;
    std::string name;
    std::string password;
    Currency currency;
    float balance;
};

struct ClientCallbackDetails {
    uint32_t id;
    sockaddr_in client_sock;
    std::chrono::steady_clock::time_point expiry;

    // overload > operator
    bool operator > (const ClientCallbackDetails& other) const {
        return expiry > other.expiry;
    }
};

void marshallStrings(uint8_t* p, std::string s, int* offset) {
    // have to cast size_t to a fixed size. Limit to 32 bits
    uint32_t s_len = htonl(static_cast<uint32_t>(s.length()));
    std::memcpy(p+*offset, &s_len, sizeof(uint32_t)); // push string length
    *offset += sizeof(uint32_t);
    std::memcpy(p+*offset, s.c_str(), s.length()); // push string characters
    *offset += s.length();
}

void marshallInt32(uint8_t* p, uint32_t i, int* offset) {
    uint32_t i_tmp = htonl(i);
    std::memcpy(p+*offset, &i_tmp, sizeof(uint32_t));
    *offset += sizeof(uint32_t);
}

void marshallFloat32(uint8_t *p, float f, int* offset) {
    uint32_t f_tmp;
    std::memcpy(&f_tmp, &f, sizeof(float));
    marshallInt32(p, f_tmp, offset);
}

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
// BankService: Core Business Logic
// ==========================================
class BankService {
private:
    std::map<uint32_t, BankAccount> accountDatabase;
    uint32_t nextAccountNumber = 0;

    // Exchange rate table: [from][to], indexed by (Currency - 1)
    // Currencies: USD=1, JPY=2, SGD=3
    // Row = source currency, Col = dest currency
    const float exchangeRates[3][3] = {
        // USD->USD  USD->JPY  USD->SGD
        { 1.0f,     149.50f,  1.35f  },
        // JPY->USD  JPY->JPY  JPY->SGD
        { 0.0067f,  1.0f,     0.009f },
        // SGD->USD  SGD->JPY  SGD->SGD
        { 0.74f,    111.0f,   1.0f   }
    };

public:
    std::vector<uint8_t> openAccount(
        int opcode,
        const std::string& name, 
        const std::string& pw, 
        Currency curr, 
        float balance, 
        std::vector<ClientCallbackDetails>* clientsMonitoring,
        const SOCKET sock
    ) {
        BankAccount acc = { nextAccountNumber++, name, pw, curr, balance };
        accountDatabase[acc.accountNumber] = acc;
        std::cout << "Created account " << acc.accountNumber << " for " << name << std::endl;
        std::printf("Unmarshalled details: name: %s, password: %s, currency: %d, balance: %.2f", name.c_str(), pw.c_str(), curr, balance);

        std::string reply_id = "SUCCESS";
        std::vector<uint8_t> buffer;
        int offset = 0;
        buffer.resize(sizeof(uint32_t)*3 + reply_id.length()); // set string length to uint32_t
        marshallInt32(buffer.data(), opcode, &offset);
        marshallStrings(buffer.data(), reply_id, &offset);
        marshallInt32(buffer.data(), acc.accountNumber, &offset);

        // send to monitoring clients
        for (size_t i = 0; i < clientsMonitoring->size(); i++) {
            sockaddr_in client_sock = (*clientsMonitoring)[i].client_sock;
            sendto(sock, reinterpret_cast<const char*>(buffer.data()), buffer.size(), 0, (struct sockaddr*)&client_sock, sizeof(client_sock));
        }

        return buffer;
        // return "Account Created: " + std::to_string(acc.accountNumber);
    }

    std::vector<uint8_t> closeAccount(
        int opcode,
        const std::string& name, 
        uint32_t accNum, 
        const std::string& pw, 
        std::vector<ClientCallbackDetails>* clientsMonitoring,
        const SOCKET sock
    ) {
        std::string reply_id;
        std::vector<uint8_t> buffer;
        int offset;
        if (accountDatabase.count(accNum) && accountDatabase[accNum].name == name && accountDatabase[accNum].password == pw) {
            accountDatabase.erase(accNum);
            std::cout << "Closed account " << accNum << std::endl;
            std::string reply_msg = "Account " + std::to_string(accNum) + " closed successfully.";

            reply_id = "SUCCESS";
            buffer.resize(sizeof(uint32_t) * 3 + reply_id.length() + reply_msg.length());
            offset = 0;
            marshallInt32(buffer.data(), opcode, &offset);
            marshallStrings(buffer.data(), reply_id, &offset);
            marshallStrings(buffer.data(), reply_msg, &offset);

            for (size_t i = 0; i < clientsMonitoring->size(); i++) {
                sockaddr_in client_sock = (*clientsMonitoring)[i].client_sock;
                sendto(sock, reinterpret_cast<const char*>(buffer.data()), buffer.size(), 0, (struct sockaddr*)&client_sock, sizeof(client_sock));
            }
            return buffer;
            // return reply_msg;
        }
        reply_id = "ERROR_ACCOUNT_NOT_FOUND";
        std::string error_msg = "Error: Invalid credentials or account not found.";
        buffer.resize(sizeof(uint32_t) * 3 + reply_id.length() + error_msg.length());
        offset = 0;
        marshallInt32(buffer.data(), opcode, &offset);
        marshallStrings(buffer.data(), reply_id, &offset);
        marshallStrings(buffer.data(), error_msg, &offset);
        return buffer;
        // return error_msg;
    }

    std::vector<uint8_t> deposit(
        int opcode,
        uint32_t accNum, 
        const std::string& name,
        const std::string& pw, 
        float amount, 
        Currency curr,
        std::vector<ClientCallbackDetails>* clientsMonitoring,
        const SOCKET sock
    ) {
        std::string reply_id;
        std::vector<uint8_t> buffer;
        int offset;
        if (accountDatabase.count(accNum) && accountDatabase[accNum].name == name && accountDatabase[accNum].password == pw) {
            int base_curr_index = static_cast<int>(accountDatabase[accNum].currency) - 1;
            int amount_curr_index = static_cast<int>(curr) - 1;
            float rate = exchangeRates[amount_curr_index][base_curr_index]; // convert potentially foreign currency to local currency
            accountDatabase[accNum].balance += amount * rate;
            std::cout << "Deposited into account " << accNum << "an amount of " << amount << CurrToStr.at(curr) << " | New Balance: " << accountDatabase[accNum].balance << CurrToStr.at(accountDatabase[accNum].currency) << std::endl;
            
            reply_id = "SUCCESS";
            buffer.resize(sizeof(uint32_t) * 7 + reply_id.length()); // 32 bits for float
            offset = 0;
            marshallInt32(buffer.data(), opcode, &offset);
            marshallStrings(buffer.data(), reply_id, &offset);
            marshallInt32(buffer.data(), accNum, &offset);
            marshallFloat32(buffer.data(), amount, &offset);
            marshallInt32(buffer.data(), static_cast<uint32_t>(curr), &offset);
            marshallFloat32(buffer.data(), accountDatabase[accNum].balance, &offset);
            marshallInt32(buffer.data(), static_cast<uint32_t>(accountDatabase[accNum].currency), &offset);

            for (size_t i = 0; i < clientsMonitoring->size(); i++) {
                sockaddr_in client_sock = (*clientsMonitoring)[i].client_sock;
                sendto(sock, reinterpret_cast<const char*>(buffer.data()), buffer.size(), 0, (struct sockaddr*)&client_sock, sizeof(client_sock));
            }

            return buffer;
            // return "New Balance: " + std::to_string(accountDatabase[accNum].balance);
        }
        reply_id = "ERROR_ACCOUNT_NOT_FOUND";
        std::string error_msg = "Error: Invalid credentials";
        offset = 0;
        buffer.resize(sizeof(uint32_t) * 3 + reply_id.length() + error_msg.length());
        marshallInt32(buffer.data(), opcode, &offset);
        marshallStrings(buffer.data(), reply_id, &offset);
        marshallStrings(buffer.data(), error_msg, &offset);
        return buffer;
        // return error_msg;
    }

    std::vector<uint8_t> withdraw(
        int opcode,
        uint32_t accNum, 
        const std::string& name,
        const std::string& pw, 
        float amount, 
        Currency curr,
        std::vector<ClientCallbackDetails>* clientsMonitoring,
        const SOCKET sock
    ) {
        std::string reply_id;
        std::vector<uint8_t> buffer;
        std::string error_msg;
        int offset;
        if (accountDatabase.count(accNum) && accountDatabase[accNum].name == name && accountDatabase[accNum].password == pw) {
            int balance_curr_index = static_cast<int>(accountDatabase[accNum].currency) - 1;
            int withdraw_curr_index = static_cast<int>(curr) - 1;
            float rate = exchangeRates[withdraw_curr_index][balance_curr_index];

            if (accountDatabase[accNum].balance < amount * rate) {
                reply_id = "ERROR_INSUFFICIENT_BALANCE";
                error_msg = "Error: Insufficient balance.";
                buffer.resize(sizeof(uint32_t)*3 + reply_id.length() + error_msg.length());
                offset = 0;
                marshallInt32(buffer.data(), opcode, &offset);
                marshallStrings(buffer.data(), reply_id, &offset);
                marshallStrings(buffer.data(), error_msg, &offset);
                return buffer;
                // return "Error: Insufficient balance.";
            }
            accountDatabase[accNum].balance -= amount * rate;
            std::cout << "Withdrew from account " << accNum << " an amount of " << amount << CurrToStr.at(curr) << " | New Balance: " << accountDatabase[accNum].balance << CurrToStr.at(accountDatabase[accNum].currency) << std::endl;
            reply_id = "SUCCESS";
            buffer.resize(sizeof(uint32_t)*7 + reply_id.length());
            offset = 0;
            marshallInt32(buffer.data(), opcode, &offset);
            marshallStrings(buffer.data(), reply_id, &offset);
            marshallInt32(buffer.data(), accNum, &offset);
            marshallFloat32(buffer.data(), amount, &offset);
            marshallInt32(buffer.data(), static_cast<uint32_t>(curr), &offset);
            marshallFloat32(buffer.data(), accountDatabase[accNum].balance, &offset);
            marshallInt32(buffer.data(), static_cast<uint32_t>(accountDatabase[accNum].currency), &offset);

            for (size_t i = 0; i < clientsMonitoring->size(); i++) {
                sockaddr_in client_sock = (*clientsMonitoring)[i].client_sock;
                sendto(sock, reinterpret_cast<const char*>(buffer.data()), buffer.size(), 0, (struct sockaddr*)&client_sock, sizeof(client_sock));
            }

            return buffer;
            // return "New Balance: " + std::to_string(accountDatabase[accNum].balance);
        }
        reply_id = "ERROR_ACCOUNT_NOT_FOUND";
        error_msg = "Error: Invalid credentials";
        buffer.resize(sizeof(uint32_t)*3 + reply_id.length() + error_msg.length());
        offset = 0;
        marshallInt32(buffer.data(), opcode, &offset);
        marshallStrings(buffer.data(), reply_id, &offset);
        marshallStrings(buffer.data(), error_msg, &offset);
        return buffer;
        // return "Error: Invalid credentials.";
    }

    std::vector<uint8_t> checkBalance(
        int opcode,
        uint32_t accNum,
        const std::string& pw,
        std::vector<ClientCallbackDetails>* clientsMonitoring,
        const SOCKET sock
    ) {
        std::string reply_id;
        std::vector<uint8_t> buffer;
        int offset;
        if (accountDatabase.count(accNum) && accountDatabase[accNum].password == pw) {
            std::cout << "Checked balance for account " << accNum << std::endl;
            reply_id = "SUCCESS";
            buffer.resize(sizeof(uint32_t) * 4 + reply_id.length()); // opcode + replyId + accNum + balance(float)
            offset = 0;
            marshallInt32(buffer.data(), opcode, &offset);
            marshallStrings(buffer.data(), reply_id, &offset);
            marshallInt32(buffer.data(), accNum, &offset);
            marshallFloat32(buffer.data(), accountDatabase[accNum].balance, &offset);

            // Notify monitoring clients
            for (size_t i = 0; i < clientsMonitoring->size(); i++) {
                sockaddr_in client_sock = (*clientsMonitoring)[i].client_sock;
                sendto(sock, reinterpret_cast<const char*>(buffer.data()), buffer.size(), 0, (struct sockaddr*)&client_sock, sizeof(client_sock));
            }

            return buffer;
        }
        reply_id = "ERROR_ACCOUNT_NOT_FOUND";
        std::string error_msg = "Error: Invalid credentials";
        offset = 0;
        buffer.resize(sizeof(uint32_t) * 3 + reply_id.length() + error_msg.length());
        marshallInt32(buffer.data(), opcode, &offset);
        marshallStrings(buffer.data(), reply_id, &offset);
        marshallStrings(buffer.data(), error_msg, &offset);
        return buffer;
    }

    std::vector<uint8_t> transferFunds(
        int opcode,
        uint32_t srcAccNum,
        const std::string& pw,
        uint32_t dstAccNum,
        float amount,
        std::vector<ClientCallbackDetails>* clientsMonitoring,
        const SOCKET sock
    ) {
        std::string reply_id;
        std::vector<uint8_t> buffer;
        int offset;
        std::string error_msg;

        // Validate source account
        if (!accountDatabase.count(srcAccNum) || accountDatabase[srcAccNum].password != pw) {
            reply_id = "ERROR_ACCOUNT_NOT_FOUND";
            error_msg = "Error: Source account not found or invalid credentials.";
            buffer.resize(sizeof(uint32_t) * 3 + reply_id.length() + error_msg.length());
            offset = 0;
            marshallInt32(buffer.data(), opcode, &offset);
            marshallStrings(buffer.data(), reply_id, &offset);
            marshallStrings(buffer.data(), error_msg, &offset);
            return buffer;
        }

        // Validate destination account
        if (!accountDatabase.count(dstAccNum)) {
            reply_id = "ERROR_DEST_NOT_FOUND";
            error_msg = "Error: Destination account not found.";
            buffer.resize(sizeof(uint32_t) * 3 + reply_id.length() + error_msg.length());
            offset = 0;
            marshallInt32(buffer.data(), opcode, &offset);
            marshallStrings(buffer.data(), reply_id, &offset);
            marshallStrings(buffer.data(), error_msg, &offset);
            return buffer;
        }

        // Check same account
        if (srcAccNum == dstAccNum) {
            reply_id = "ERROR_SAME_ACCOUNT";
            error_msg = "Error: Source and destination accounts are the same.";
            buffer.resize(sizeof(uint32_t) * 3 + reply_id.length() + error_msg.length());
            offset = 0;
            marshallInt32(buffer.data(), opcode, &offset);
            marshallStrings(buffer.data(), reply_id, &offset);
            marshallStrings(buffer.data(), error_msg, &offset);
            return buffer;
        }

        // Check sufficient balance
        if (accountDatabase[srcAccNum].balance < amount) {
            reply_id = "ERROR_INSUFFICIENT_BALANCE";
            error_msg = "Error: Insufficient balance in source account.";
            buffer.resize(sizeof(uint32_t) * 3 + reply_id.length() + error_msg.length());
            offset = 0;
            marshallInt32(buffer.data(), opcode, &offset);
            marshallStrings(buffer.data(), reply_id, &offset);
            marshallStrings(buffer.data(), error_msg, &offset);
            return buffer;
        }

        // Apply exchange rate
        int srcCurrIdx = static_cast<int>(accountDatabase[srcAccNum].currency) - 1;
        int dstCurrIdx = static_cast<int>(accountDatabase[dstAccNum].currency) - 1;
        float rate = exchangeRates[srcCurrIdx][dstCurrIdx];
        float dstAmount = amount * rate;

        // Execute transfer
        accountDatabase[srcAccNum].balance -= amount;
        accountDatabase[dstAccNum].balance += dstAmount;

        float newSrcBalance = accountDatabase[srcAccNum].balance;
        float newDstBalance = accountDatabase[dstAccNum].balance;

        std::cout << "Transfer: " << amount << " from account " << srcAccNum
                  << " -> " << dstAmount << " to account " << dstAccNum
                  << " (rate: " << rate << ")" << std::endl;

        // Build success response:
        // opcode(4) + SUCCESS(4+7) + srcAcc(4) + dstAcc(4) + srcAmt(4) + dstAmt(4)
        // + newSrcBal(4) + newDstBal(4) + rate(4) + srcCurrency(4) + dstCurrency(4) = 51 bytes
        reply_id = "SUCCESS";
        buffer.resize(sizeof(uint32_t) * 11 + reply_id.length()); // 44 + 7 = 51 bytes
        offset = 0;
        marshallInt32(buffer.data(), opcode, &offset);
        marshallStrings(buffer.data(), reply_id, &offset);
        marshallInt32(buffer.data(), srcAccNum, &offset);
        marshallInt32(buffer.data(), dstAccNum, &offset);
        marshallFloat32(buffer.data(), amount, &offset);
        marshallFloat32(buffer.data(), dstAmount, &offset);
        marshallFloat32(buffer.data(), newSrcBalance, &offset);
        marshallFloat32(buffer.data(), newDstBalance, &offset);
        marshallFloat32(buffer.data(), rate, &offset);
        marshallInt32(buffer.data(), static_cast<int>(accountDatabase[srcAccNum].currency), &offset);
        marshallInt32(buffer.data(), static_cast<int>(accountDatabase[dstAccNum].currency), &offset);

        // Notify monitoring clients
        for (size_t i = 0; i < clientsMonitoring->size(); i++) {
            sockaddr_in client_sock = (*clientsMonitoring)[i].client_sock;
            sendto(sock, reinterpret_cast<const char*>(buffer.data()), buffer.size(), 0, (struct sockaddr*)&client_sock, sizeof(client_sock));
        }

        return buffer;
    }
};

// ==========================================
// MessageParser: UDP Payload Unmarshalling
// ==========================================
class MessageParser {
private:
    std::map<std::string, std::vector<uint8_t>> requestHistory;
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
    std::vector<uint8_t> processMessage(const char* buffer, const SOCKET sock, sockaddr_in client_socketAddr, BankService& bank) {
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

        std::vector<uint8_t> response;
        std::string reply_id;
        std::string reply_msg;
        std::string error_msg;

        switch (opcode) {
            case 1: { // Open Account
                std::string name = readString(buffer, offset);
                std::string pw = readString(buffer, offset);
                Currency curr = static_cast<Currency>(readInt32(buffer, offset));
                float balance = readFloat(buffer, offset);
                response = bank.openAccount(opcode, name, pw, curr, balance, &(clientsMonitoring.internal_vector_form()), sock);
                break;
            }
            case 2: { // Close Account
                std::string name = readString(buffer, offset);
                uint32_t accNum = readInt32(buffer, offset);
                std::string pw = readString(buffer, offset);
                response = bank.closeAccount(opcode, name, accNum, pw, &(clientsMonitoring.internal_vector_form()), sock);
                break;
            }
            case 3: { // Deposit
                uint32_t accNum = readInt32(buffer, offset);
                std::string name = readString(buffer, offset);
                std::string pw = readString(buffer, offset);
                float amount = readFloat(buffer, offset);
                Currency cur = static_cast<Currency>(readInt32(buffer, offset));
                response = bank.deposit(opcode, accNum, name, pw, amount, cur, &(clientsMonitoring.internal_vector_form()), sock);
                break;
            }
            case 4: { // Withdraw
                uint32_t accNum = readInt32(buffer, offset);
                std::string name = readString(buffer, offset);
                std::string pw = readString(buffer, offset);
                float amount = readFloat(buffer, offset);
                Currency cur = static_cast<Currency>(readInt32(buffer, offset));
                response = bank.withdraw(opcode, accNum, name, pw, amount, cur, &(clientsMonitoring.internal_vector_form()), sock);
                break;
            }
            case 5: { // Monitor
                int64_t duration = readLong64(buffer, offset);
                std::cout << "duration: " << duration << std::endl;
                auto expiry = std::chrono::steady_clock::now() + std::chrono::nanoseconds(duration);
                m.lock();
                clientsMonitoring.push({clientId++, client_socketAddr, expiry});
                for (const auto& item : clientsMonitoring.internal_vector_form()) {
                    std::cout << "Testing: " << item.id << ": " << item.client_sock.sin_port << std::endl;
                }
                std::cout << "------" << std::endl;
                m.unlock();
                client_added.notify_one(); // wake 
                reply_id = "ACK";
                reply_msg = std::string("Server will send updates for ") + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::nanoseconds(duration)).count()) + std::string(" seconds");
                offset = 0;
                response.resize(sizeof(uint32_t)*3 + reply_id.length() + reply_msg.length());
                marshallInt32(response.data(), opcode, &offset);
                marshallStrings(response.data(), reply_id, &offset);
                marshallStrings(response.data(), reply_msg, &offset);
                break;
            }
            case 6: { // Check Balance (Idempotent)
                uint32_t accNum = readInt32(buffer, offset);
                std::string pw = readString(buffer, offset);
                response = bank.checkBalance(opcode, accNum, pw, &(clientsMonitoring.internal_vector_form()), sock);
                break;
            }
            case 7: { // Transfer Funds (Non-Idempotent)
                uint32_t srcAccNum = readInt32(buffer, offset);
                std::string pw = readString(buffer, offset);
                uint32_t dstAccNum = readInt32(buffer, offset);
                float amount = readFloat(buffer, offset);
                response = bank.transferFunds(opcode, srcAccNum, pw, dstAccNum, amount, &(clientsMonitoring.internal_vector_form()), sock);
                break;
            }
            default:
                reply_id = "ERROR";
                error_msg = "Unknown Opcode" + std::to_string(opcode);
                response.resize(sizeof(uint32_t)*3 + reply_id.length() + error_msg.length());
                marshallInt32(response.data(), opcode, &offset);
                marshallStrings(response.data(), reply_id, &offset);
                marshallStrings(response.data(), error_msg, &offset);
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

    void start(BankService& bank, MessageParser& parser, float drop_request, float drop_reply) {
        char buffer[1024]{}; 
        sockaddr_in client_socketAddr{};
        int len_client_socketAddr = sizeof(client_socketAddr); 
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);

        while (true) {
            memset(buffer, 0, SIZE);
            int bytesReceived = recvfrom(sock_des, buffer, SIZE, 0, (struct sockaddr*)&client_socketAddr, &len_client_socketAddr);
            
            if (bytesReceived == SOCKET_ERROR) continue;

            if (drop_request > 0.0 && dis(gen) < drop_request) {
                std::cout << "[SIMULATION] Dropped incoming request from client." << std::endl;
                continue;
            }

            std::vector<uint8_t> response = parser.processMessage(buffer, sock_des, client_socketAddr, bank);
            std::cout << std::endl << "size: " << response.size() << std::endl;
            for (size_t i = 0; i < response.size(); ++i) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(response[i]) << " ";
            }
            std::cout << std::dec << std::endl;

            if (drop_reply > 0.0 && dis(gen) < drop_reply) {
                std::cout << "[SIMULATION] Dropped outgoing reply to client." << std::endl;
                continue;
            }

            sendto(sock_des, reinterpret_cast<const char*>(response.data()), response.size(), 0, (struct sockaddr*)&client_socketAddr, sizeof(client_socketAddr));
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

    std::regex r_drop_req(R"(--drop_request=([0-9]*\.?[0-9]+))");
    float drop_request = 0.0f;
    if (std::regex_search(options, m, r_drop_req)) {
        drop_request = stof(m[1].str());
    }

    std::regex r_drop_rep(R"(--drop_reply=([0-9]*\.?[0-9]+))");
    float drop_reply = 0.0f;
    if (std::regex_search(options, m, r_drop_rep)) {
        drop_reply = stof(m[1].str());
    }

    BankService bank;
    MessageParser parser(semantics);
    UDPServer server(port);

    std::cout << "Starting Server with semantics: " << semantics 
              << ", drop_request: " << drop_request << ", drop_reply: " << drop_reply << std::endl;
    server.start(bank, parser, drop_request, drop_reply);

    return 0;
}