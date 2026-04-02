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

// ==============================================================================================
// Utility functions for marshalling strings, integers (signed and unsigned), and floats
// ==============================================================================================
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

// ==============================================================================================
// IterablePriorityQueue: inherits from priority queue, exposes its private container (iterable)
// ==============================================================================================
template <typename T, typename Container = std::vector<T>, typename Compare = std::less<T>>
class IterablePriorityQueue : public std::priority_queue<T, Container, Compare> {
public:
    Container& internal_vector_form() {
        return this->c;
    }
};

// ==============================================================================================
// BankService: Core Business Logic for 6 of the 7 main operations (excluding Monitor)
// private variables:
// 1. accountDatabase: in-memory storage for bank accounts. Maps bank account number to
//                      bank account details stored in the BankAccount struct
// 2. nextAccountNumber: next bank account number to be assigned to the next newly
//                       created bank account
// 3. exchangeRates: 2-dimensional array containing exchange rate for currency type
//                   x->y accessible via exchangeRates[x][y], or the reverse.
// public functions:
// 1. openAccount, 2. closeAccount, 3. deposit, 4. withdraw, 5. checkBalance, 6. transferFunds
// ==============================================================================================
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
//===============================================================================================
// openAccount: Function that implements the open account service
//              Creates a new account with data in request, and notifies monitoring clients on success
//              Creates a new BankAccount based on user inpt (below), adds mapping between
//              incrementing nextAccountNumber and BankAccount. Returns a byte vector containing
//              marshalled fields as response message.
// Parameters:
// 1. opcode: unmarshalled opcode (1 for opening an account),
// 2. name: unmarshalled user provided name,
// 3. pw: unmarshalled user provided password,
// 4. curr: unmarshalled Integer constant converted to a Currency type enum,
// 5. balance: unmarshalled float for initial account balance,
// 6. clientsMonitoring: vector/iterable list of monitoring clients (whose monitor interval have
//                       yet to expire)
// 7. sock: SOCKET object used to send updates to each of the monitoring clients
//
// Returns:
// Byte vector: || int opcode || int reply_id_len || string reply_id || uint32_t accountNumber
//===============================================================================================
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
    }

//============================================================================================
// closeAccount: Function that implements the close account service. 
//               Closes an existing account identified by account number.
//               Checks whether name and pw matches with corresponding name and pw of account
//
// Returns:
// 1. Byte vector containing SUCCESS IF account number is found in accountDatabase, name and pw
//    match. Vector contains the following marshalled fields: 
//    || int opcode || int reply_id_len || string reply_id || int reply_msg_len || string reply_msg
// 2. Byte vector containing ERROR_ACCOUNT_NOT_FOUND IF account number not found in accountDatabase,
//    or no match for name or pw. Vector contains the following marshalled fields:
//    || int opcode || int reply_id_len || string reply_id || int error_msg_len || string error_msg
//============================================================================================
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
        }
        reply_id = "ERROR_ACCOUNT_NOT_FOUND";
        std::string error_msg = "Error: Invalid credentials or account not found.";
        buffer.resize(sizeof(uint32_t) * 3 + reply_id.length() + error_msg.length());
        offset = 0;
        marshallInt32(buffer.data(), opcode, &offset);
        marshallStrings(buffer.data(), reply_id, &offset);
        marshallStrings(buffer.data(), error_msg, &offset);
        return buffer;
    }

//============================================================================================
// deposit: Function that implements the deposit service.
//          Deposits an amount of a specified currency type into an account
//          If the currency type of the initial balance of the account differs from the deposited
//          amount, the amount is first converted to the native type before being added.
//          Sends update to other monitoring clients if deposit operation is successful.
//
// Returns:
// 1. If account exists, name and pw matches the name and password in-memory, then deposit()
//    returns a byte vector with the following marshalled fields:
//    ++------------++------------------++-----------------++-------------------------++
//    || int opcode || int reply_id_len || string reply_id || uint32_t account_number ||
//    ++------------++------------------++-----------------++-------------------------++
//    || float amt  || int currency_type|| float new_bal   || int acc_currency_type   ||
//    ++------------++------------------++-----------------++-------------------------++ 
// 2. If account does not exist or invalid credentials were provided, returns a byte vector 
//    with reply_id = ERROR_ACCOUNT_NOT_FOUND, and a corresponding error message instead.
//=============================================================================================
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

//=============================================================================================
// withdraw: Function that implements the withdrawal service.
//           Withdraws an amount of a specified currency type from an existing
//           account. If the currency type to be withdrawn differs from the native type, the
//           withdrawal amount is first converted to the native type before being deducted.
//           Sends update to other monitoring clients if withdrawal is successful.
// 
// Returns:
// 1. If account exists, name and pw matches the name and password in accountDatabase, then
//    withdraw() returns a byte vector with the following marshalled types:
//    ++------------++------------------++-----------------++-------------------------++
//    || int opcode || int reply_id_len || string reply_id || uint32_t account_number ||
//    ++------------++------------------++-----------------++-------------------------++
//    || float amt  || int currency_type|| float new_bal   || int acc_currency_type   ||
//    ++------------++------------------++-----------------++-------------------------++ 
// 2. If there is insufficient balance in the account to make a withdrawal, a byte vector 
//    with reply_id = "ERROR_INSUFFICIENT_BALANCE" and an accompanying error message is returned
//    instead.
// 3. If account does not exist or invalid credentials were provided, returns a byte vector 
//    with reply_id = ERROR_ACCOUNT_NOT_FOUND, and a corresponding error message instead. 
//=============================================================================================
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
        }
        reply_id = "ERROR_ACCOUNT_NOT_FOUND";
        error_msg = "Error: Invalid credentials";
        buffer.resize(sizeof(uint32_t)*3 + reply_id.length() + error_msg.length());
        offset = 0;
        marshallInt32(buffer.data(), opcode, &offset);
        marshallStrings(buffer.data(), reply_id, &offset);
        marshallStrings(buffer.data(), error_msg, &offset);
        return buffer;
    }

//=============================================================================================
// checkBalance: Function that implements the check balance service.
//               It returns the current balance of a specified account and sends an update
//               to other monitoring clients if operation was successful.
// Returns:
// 1. If account exists, and password given matches the copy stored in accountDatabase, then
//    checkBalance() returns a byte vector containing the account number and the balance, in
//    addition to the opcode and reply_id like other functions.
// 2. If account does not exist, or password does not match, then a byte vector with reply_id
//    ERROR_ACCOUNT_NOT_FOUND as well as the corresponding error message is returned instead.
//=============================================================================================
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

//=============================================================================================
// transferFunds: Function that implements the transfer funds service.
//                Withdraws an amount from a source account and transfers that amount to be deposited
//                into a destination account. If currency type differs between accounts, the amount
//                is first converted to the destination account currency type.
//                Sends an update to other monitoring clients if successful.
//                
// Returns:
// 1. If both the source and destination account exists, and the password provided for the source
//    account matches the entry in the accountDatabase, then transferFunds() returns the byte vector
//    containing the following marshalled fields:
//    - dest_amount is the amount converted to the destination account currency type (determined
//    by exchange rate)
//    - rate: exchange rate between the currencies of the source and destination accounts
//    ++------------++------------------++-----------------++-----------------------------++
//    || int opcode || int reply_id_len || string reply_id || uint32_t src_account_number ||
//    ++--------------------------------++-----------------++-----------------------------++
//    || uint32_t dst_account_number    || float amount    || float dest_amount           ||
//    ++--------------------------------++-----------------++-----------------------------++
//    || float new_src_account_bal      || float new_dst_account_bal || float rate        ||
//    ++--------------------------------++------------------------------------------------++
//    || int src_acc_currency_type      || int dst_acc_currency_type                      ||
//    ++--------------------------------++------------------------------------------------++
// 2. If the source account does not exist or the pw does not match, then a byte vector with reply_id
//    ERROR_ACCOUNT_NOT_FOUND as well as the corresponding error message will be returned.
// 3. If the destination account does not exist, a byte vector with reply_id, ERROR_DEST_NOT_FOUND
//    and the corresponding error message will be returned instead.
// 4. If the source account does not contain sufficient balance, then a byte vector with reply_id
//    ERROR_INSUFFICIENT_BALANCE and the corresponding error message will be returned.
//=============================================================================================
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

// =============================================================================================
// MessageParser: UDP Payload Unmarshalling. Unmarshalls the request payload. First to retrieve
//                the opcode to determine the type of service requested, then within the switch
//                case itself, the rest of the fields are unmarshalled with utility functions
//                defined within the class to be passed into the implementations that live in
//                the BankService class.
// private variables:
// 1. requestHistory:    Used to implement At-most-once invocation semantics, in which responses to
//                       requests are stored in main memory to prevent re-execution on retransmission.
// 2. semantics:         The string representation of one of two invocation semantics (At-least-once/At-most-once)
//                       Initialised upon instantiation of the MessageParser class.
// 3. clientsMonitoring: A iterable min-heap which stores monitoring client details
// 4. clientId:          Unique identifier for each record in clientsMonitoring. Incremented on each addition
// 5. m:                 A lock that has to be acquired before any modification can be made to clientsMonitoring
// 6. client_added:      A condition variable used to notify the cleaner thread that a new client record
//                       has been added to clientsMonitoring
// 7. running:           A boolean that controls the running status of the cleaner thread. On destruction,
//                       all background threads are waked then joined before the main thread exits.
// 8. cleaner_thread:    The thread that deletes client records from clientsMonitoring once the monitor
//                       interval has expired.
// 
// Unmarshalling functions for int, float, strings, longs: readInt32(), readFloat(), readString(), readLong64()
// Accepts a buffer which contains the request payload (passed by UDPServer), and updates offset on each read.
// 
// Switch cases unmarshalls rest of fields and waits for a response to be returned which it passes
// onto UDPServer for the actual transmission of the server response.
// =============================================================================================
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
        int64_t net_val = (static_cast<uint64_t>(tmp) << 32);
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

        // Unique request identifier to differentiate between multiple clients sending requests
        std::string clientKey = std::string(inet_ntoa(client_socketAddr.sin_addr)) + ":" + 
                                std::to_string(ntohs(client_socketAddr.sin_port)) + "-" + 
                                std::to_string(requestId);

        // if key is re-encountered on receiving request, send the response stored in requestHistory instead (AMO only)
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
                auto expiry = std::chrono::steady_clock::now() + std::chrono::nanoseconds(duration); // use monotonic clock to calculate monitor interval expiry
                m.lock(); // acquire lock before modifying clientsMonitoring
                clientsMonitoring.push({clientId++, client_socketAddr, expiry});
                for (const auto& item : clientsMonitoring.internal_vector_form()) {
                    std::cout << "Testing: " << item.id << ": " << item.client_sock.sin_port << std::endl;
                }
                std::cout << "------" << std::endl;
                m.unlock();
                client_added.notify_one(); // release lock then notify cleaner_thread

                // send an acknowledgement back to the client, so it can block
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
                client_added.wait(lock, [this] { return !clientsMonitoring.empty() || !running; }); // wake if new record added
            } else {
                auto nextExpiry = clientsMonitoring.top().expiry;
                if (nextExpiry <= std::chrono::steady_clock::now()) { // check first whether already expired
                    clientsMonitoring.pop();
                    continue;
                }
                bool status = client_added.wait_until(lock, nextExpiry, [this, &nextExpiry] {
                    return !running || (!clientsMonitoring.empty() && clientsMonitoring.top().expiry < nextExpiry);
                }); // lock reacquired on notification, predicate true, or on next expiry
                if (status) // predicate evaluates to true, means the newly added client expires sooner than the one currently being tracked
                    continue;
                else { // nextExpiry of current tracked entry has elapsed, pop
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
            
            // print received request
            std::cout << std::endl << "request size: " << bytesReceived << std::endl;
            for (size_t i = 0; i < bytesReceived; ++i) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]) << " ";
            }
            std::cout << std::dec << std::endl;

            if (drop_request > 0.0 && dis(gen) < drop_request) {
                std::cout << "[SIMULATION] Dropped incoming request from client." << std::endl;
                continue;
            }

            // prints outgoing response
            std::vector<uint8_t> response = parser.processMessage(buffer, sock_des, client_socketAddr, bank);
            std::cout << std::endl << "response size: " << response.size() << std::endl;
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