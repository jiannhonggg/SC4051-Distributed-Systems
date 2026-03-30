# SC4051-Distributed-Systems

Commands to get the program up and running. 

Server Side : 
g++ server.cpp -o server.exe -lws2_32
./server (amo or alo)
amo for at-most-once, alo for at-least-once invocation semantics
Options: --server_port=<> (Specify server port). Server listens to requests from all network interfaces.

cd ./client-java
javac *.java 
java MainApp 
Options: --client_port=<> (Client port for socket creation), --server_port=<>, --server_ip=<>

Completed: 

1. Open Account 
2. Close Account 
3. Deposit 
4. Withdraw 
5. Monitor Account (Callback)

To Do: 
1. Check Balance (idempotent operation)
2. Transfer Funds (non-idemopotent operation)
3. Failure Simulation 