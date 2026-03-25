import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Scanner;

public class MainApp {
    public static void main(String[] args) {
        Scanner sc = new Scanner(System.in);
        try {
            ClientSocket client = new ClientSocket();
            boolean running = true;
            int requestId = 0; 

            while (running) {
                System.out.println("\n--- Distributed Bank Menu ---");
                System.out.println("1. Open Account");
                System.out.println("2. Close Account");
                System.out.println("3. Deposit");
                System.out.println("4. Withdraw");
                System.out.println("5. Monitor");
                System.out.println("q. Quit");

                System.out.print("Choice: ");
                String choice = sc.nextLine();
                if (choice.equalsIgnoreCase("q")) break;

                byte[] requestPayload = null;

                switch (choice) {
                    case "1": // Open Account
                        System.out.print("Name: "); String name = sc.nextLine();
                        System.out.print("Password: "); String pw = sc.nextLine();
                        System.out.print("Currency (1:USD, 2:EUR): "); int curr = Integer.parseInt(sc.nextLine());
                        System.out.print("Initial Balance: "); float bal = Float.parseFloat(sc.nextLine());

                        byte[] nameBytes = name.getBytes(StandardCharsets.UTF_8);
                        byte[] pwBytes = pw.getBytes(StandardCharsets.UTF_8);

                        // Layout: Op(4) + ReqID(4) + NameLen(4) + Name(n) + PwLen(4) + Pw(m) + Curr(4) + Bal(4)
                        ByteBuffer openBuf = ByteBuffer.allocate(24 + nameBytes.length + pwBytes.length);
                        openBuf.order(ByteOrder.BIG_ENDIAN);
                        openBuf.putInt(Constants.OP_OPEN);
                        openBuf.putInt(++requestId);
                        openBuf.putInt(nameBytes.length);
                        openBuf.put(nameBytes);
                        openBuf.putInt(pwBytes.length);
                        openBuf.put(pwBytes);
                        openBuf.putInt(curr);
                        openBuf.putFloat(bal);
                        requestPayload = openBuf.array();
                        break;

                    case "2": // Close Account
                        System.out.print("Name: "); String cName = sc.nextLine();
                        System.out.print("Account Number: "); int accNum = Integer.parseInt(sc.nextLine());
                        System.out.print("Password: "); String cPw = sc.nextLine();

                        byte[] cNameBytes = cName.getBytes(StandardCharsets.UTF_8);
                        byte[] cPwBytes = cPw.getBytes(StandardCharsets.UTF_8);

                        // Layout: Op(4) + ReqID(4) + NameLen(4) + Name(n) + AccNum(4) + PwLen(4) + Pw(m)
                        ByteBuffer closeBuf = ByteBuffer.allocate(20 + cNameBytes.length + cPwBytes.length);
                        closeBuf.order(ByteOrder.BIG_ENDIAN);
                        closeBuf.putInt(Constants.OP_CLOSE);
                        closeBuf.putInt(++requestId);
                        closeBuf.putInt(cNameBytes.length);
                        closeBuf.put(cNameBytes);
                        closeBuf.putInt(accNum);
                        closeBuf.putInt(cPwBytes.length);
                        closeBuf.put(cPwBytes);
                        requestPayload = closeBuf.array();
                        break;

                    case "3": // Deposit
                    case "4": // Withdraw
                        int opCode = choice.equals("3") ? Constants.OP_DEPOSIT : Constants.OP_WITHDRAW;
                        System.out.print("Account Number: "); int dAcc = Integer.parseInt(sc.nextLine());
                        System.out.print("Password: "); String dPw = sc.nextLine();
                        System.out.print("Amount: "); float amt = Float.parseFloat(sc.nextLine());

                        byte[] dPwBytes = dPw.getBytes(StandardCharsets.UTF_8);

                        // Layout: Op(4) + ReqID(4) + AccNum(4) + PwLen(4) + Pw(m) + Amt(4)
                        ByteBuffer transBuf = ByteBuffer.allocate(20 + dPwBytes.length);
                        transBuf.order(ByteOrder.BIG_ENDIAN);
                        transBuf.putInt(opCode);
                        transBuf.putInt(++requestId);
                        transBuf.putInt(dAcc);
                        transBuf.putInt(dPwBytes.length);
                        transBuf.put(dPwBytes);
                        transBuf.putFloat(amt);
                        requestPayload = transBuf.array();
                        break;

                    case "5": // Monitor (Callback registration)
                        System.out.print("Enter monitor interval (seconds): ");
                        int interval = Integer.parseInt(sc.nextLine());
                        
                        ByteBuffer monBuf = ByteBuffer.allocate(12);
                        monBuf.order(ByteOrder.BIG_ENDIAN);
                        monBuf.putInt(Constants.OP_MONITOR);
                        monBuf.putInt(++requestId);
                        monBuf.putInt(interval);
                        requestPayload = monBuf.array();
                        break;
                }

                if (requestPayload != null) {
                    client.sendRequest(requestPayload);
                    // For now, receive response as a simple string for testing
                    // Eventually, you'll need a binary unmarshaller here too
                    String response = client.receiveResponse();
                    System.out.println("\n[SERVER RESPONSE]: " + response);

                    // If we just registered for monitoring, enter a listen loop
                    if (choice.equals("5")) {
                        System.out.println("Monitoring for updates...");
                        // Simplified: in a real implementation, you'd loop for the interval duration [cite: 59, 64]
                        String update = client.receiveResponse(); 
                        System.out.println("[CALLBACK UPDATE]: " + update);
                    }
                }
            }
            client.close();
        } catch (Exception e) {
            System.err.println("Error: " + e.getMessage());
            e.printStackTrace();
        }
    }
}