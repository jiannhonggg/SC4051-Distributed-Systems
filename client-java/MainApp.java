import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Scanner;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.time.Duration;

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
                    case "1": // Open Account: Op(4) + ReqID(4) + NameLen(4) + Name(n) + PwLen(4) + Pw(m) + Curr(4) + Bal(4)
                        System.out.print("Name: "); String name = sc.nextLine();
                        System.out.print("Password: "); String pw = sc.nextLine();
                        System.out.print("Currency (USD, JPY, or SGD): "); Currency curr;
                        try {
                            curr = Currency.valueOf(sc.nextLine()); //TODO
                        } catch (IllegalArgumentException e) {
                            System.out.println("The currency you just typed in was not recognised. Please try again");
                            break;
                        }
                        System.out.print("Initial Balance: "); float bal;
                        try {
                            bal = Float.parseFloat(sc.nextLine());
                            if (bal < 0) {
                                System.out.println("Negative account balance is impossible");
                                break;
                            }
                        } catch (IllegalArgumentException e) {
                            System.out.println("The balance you just typed in was not recognised. Please try again");
                            break;
                        }
                        

                        byte[] nameBytes = name.getBytes(StandardCharsets.UTF_8);
                        byte[] pwBytes = pw.getBytes(StandardCharsets.UTF_8);

                        ByteBuffer openBuf = ByteBuffer.allocate(24 + nameBytes.length + pwBytes.length);
                        openBuf.order(ByteOrder.BIG_ENDIAN);
                        openBuf.putInt(Constants.OP_OPEN);
                        openBuf.putInt(++requestId);
                        openBuf.putInt(nameBytes.length);
                        openBuf.put(nameBytes);
                        openBuf.putInt(pwBytes.length);
                        openBuf.put(pwBytes);
                        openBuf.putInt(curr.getCurrencyNumber());
                        openBuf.putFloat(bal);
                        requestPayload = openBuf.array();
                        break;

                    case "2": // Close Account: Op(4) + ReqID(4) + NameLen(4) + Name(n) + AccNum(4) + PwLen(4) + Pw(m)
                        System.out.print("Name: "); String cName = sc.nextLine();
                        // Java does not have an unsigned integer type, use long then truncate to int instead
                        System.out.print("Account Number: "); int accNum;
                        try {
                            long unsigned_accNum = Long.parseLong(sc.nextLine());
                            if ((unsigned_accNum & 0x7fffffff00000000L) > 0 || unsigned_accNum < 0) {
                                System.out.println("The account number you just typed in exact maximum bounds. Please try again");
                                break;
                            }
                            accNum = (int) unsigned_accNum;
                        } catch (IllegalArgumentException e) {
                            e.printStackTrace();
                            System.out.println("The account number you just typed in was not recognised. Please try again.");
                            break;
                        }
                        System.out.print("Password: "); String cPw = sc.nextLine();

                        byte[] cNameBytes = cName.getBytes(StandardCharsets.UTF_8);
                        byte[] cPwBytes = cPw.getBytes(StandardCharsets.UTF_8);

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
                    case "4": // Withdraw: Op(4) + ReqID(4) + AccNum(4) + PwLen(4) + Pw(m) + Amt(4)
                        int opCode = choice.equals("3") ? Constants.OP_DEPOSIT : Constants.OP_WITHDRAW;
                        System.out.print("Account Number: "); int dAcc;
                        try {
                            long unsigned_dAcc = Long.parseLong(sc.nextLine());
                            if ((unsigned_dAcc & 0x7fffffff00000000L) > 0 || unsigned_dAcc < 0) {
                                System.out.println("The account number you just typed in exceeded maximum bounds. Please try again");
                                break;
                            }
                            dAcc = (int) unsigned_dAcc;
                        } catch (IllegalArgumentException e) {
                            e.printStackTrace();
                            System.out.println("The account number you just typed in was not recognised. Please try again.");
                            break;
                        }
                        System.out.print("Password: "); String dPw = sc.nextLine();
                        System.out.print("Amount: "); float amt;
                        try {
                            amt = Float.parseFloat(sc.nextLine());
                            if (amt < 0) {
                                System.out.println("The amount should not be negative. Please try again.");
                                break;
                            }
                        } catch (IllegalArgumentException e) {
                            System.out.println("The amount you just typed in was not recognised. Please try again.");
                            break;
                        }

                        byte[] dPwBytes = dPw.getBytes(StandardCharsets.UTF_8);

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
                    case "5":
                        System.out.print("Monitor Interval (Example usage - 1hrs 30mins 5secs): "); String interval = sc.nextLine();
                        long value = 0;
                        Duration d = Duration.ZERO;
                        // no negative numbers
                        Pattern p = Pattern.compile("\\b(\\d+)\\s?(h|hr|hrs|hour|hours|min|mins|minute|minutes|m|s|sec|secs|second|seconds)", Pattern.CASE_INSENSITIVE);
                        Matcher m = p.matcher(interval);

                        while (m.find()) {
                            value = Long.parseLong(m.group(1));
                            char unit = m.group(2).toLowerCase().charAt(0);
                            switch (unit) {
                                case 'h':
                                    d = d.plusHours(value);
                                    break;
                                case 'm':
                                    d = d.plusMinutes(value);
                                    break;
                                case 's':
                                    d = d.plusSeconds(value);
                                    break;
                            }
                        }

                        if (d.isZero()) {
                            System.out.println("Monitor interval is zero. Please try again.");
                            break;
                        }

                        ByteBuffer regBuff = ByteBuffer.allocate(16);
                        regBuff.order(ByteOrder.BIG_ENDIAN);
                        regBuff.putInt(Constants.OP_MONITOR);
                        regBuff.putInt(++requestId);
                        regBuff.putLong(d.toNanos());
                        requestPayload = regBuff.array();
                        break;
                }
                
                if (requestPayload != null) {
                    // Use the new sendAndReceive for At-Least-Once reliability
                    String response = client.sendAndReceive(requestPayload);
                    System.out.println("\n[SERVER RESPONSE]: " + response);
                }
            }
            client.close();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}