# SC4051-Distributed-Systems

## Setup & Running

### Server (C++)

**Compile:**
cd server-cpp
```
C:\msys64\ucrt64\bin\g++.exe server.cpp -o server.exe -lws2_32 -static-libgcc -static-libstdc++
```

**Run:**
```
server.exe amo
server.exe alo
```
- `amo` = At-Most-Once semantics (duplicate requests are filtered)
- `alo` = At-Least-Once semantics (duplicate requests may re-execute)
- Optional: `--server_port=<port>` (default: 8080)

---

### Client (Java)

**Compile:**
```
cd client-java
javac *.java
```

**Run:**
```
java MainApp
```
- Optional: `--server_ip=<ip>` (default: 127.0.0.1)
- Optional: `--server_port=<port>` (default: 8080)
- Optional: `--client_port=<port>` (default: 0, OS auto-assigns)
- Multiple clients can run simultaneously without specifying a client port.

---

### Running in NTU Lab

The NTU Lab assigns machines IP addresses in the range `10.96.x.x`. The server and client will likely be on **different machines**.

**On the Server machine:**
```
server.exe amo --server_port=8080
```

**On each Client machine (try this first):**
```
java MainApp --server_ip=10.96.x.x --server_port=8080
```
- The client port defaults to `0` (OS auto-assigns a free port). This works in most cases.
- If the lab firewall blocks randomly assigned ports, manually specify a unique port per client:
```
java MainApp --server_ip=10.96.x.x --server_port=8080 --client_port=8080   <- Client 1
java MainApp --server_ip=10.96.x.x --server_port=8080 --client_port=8081   <- Client 2
java MainApp --server_ip=10.96.x.x --server_port=8080 --client_port=8082   <- Client 3
```

---

## Features

### Completed
1. Open Account
2. Close Account
3. Deposit
4. Withdraw
5. Monitor Account (Callback)
6. Check Balance (Idempotent)

### To Do
1. Transfer Funds (Non-Idempotent)
2. Failure Simulation