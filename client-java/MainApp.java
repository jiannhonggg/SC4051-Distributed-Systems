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
            String options = String.join(" ", args);
            Pattern p = Pattern.compile("\\b--(\\w+)=([\\d.]+)\\b");
            Matcher m = p.matcher(options);
            int client_port = 0, server_port = 8080;
            String server_ip = "127.0.0.1";
            while (m.find()) {
                String key = m.group(1);
                String value = m.group(2);

                switch(key) {
                    case "client_port":
                        try {
                            client_port = Integer.parseInt(value);
                            if (client_port < 0 || client_port > 65535) {
                                System.out.println("The client port is invalid. Please try again.");
                            }
                        } catch (IllegalArgumentException e) {
                            System.out.println("The client port you just typed in was not recognised. Please try again.");
                            return;
                        }
                        break;
                    case "server_ip":
                        String[] octets = value.split(".");
                        try {
                            for (String octet : octets) {
                                int parsed_octet = Integer.parseInt(octet);
                                if (parsed_octet < 0 || parsed_octet > 255) {
                                    System.out.println("This IP address is invalid. Please try again.");
                                    return;
                                }
                            }
                            server_ip = value;
                        } catch (IllegalArgumentException e) {
                            System.out.println("The IP address you typed in was not recognised. Please try again.");
                            return;
                        }
                        break;
                    case "server_port":
                        try {
                            server_port = Integer.parseInt(value);
                            if (server_port < 0 || server_port > 65535) {
                                System.out.println("The server port is invalid. Please try again.");
                            }
                        } catch (IllegalArgumentException e) {
                            System.out.println("The server port you just typed in was not recognised. Please try again.");
                            return;
                        }
                        break;
                }
            }
            ClientSocket client = new ClientSocket(client_port, server_ip, server_port);

            boolean running = true;
            int requestId = 0; 

            while (running) {
                System.out.println("\n--- Distributed Bank Menu ---");
                System.out.println("1. Open Account");
                System.out.println("2. Close Account");
                System.out.println("3. Deposit");
                System.out.println("4. Withdraw");
                System.out.println("5. Monitor");
                System.out.println("6. Check Balance"); // Idempotent
                System.out.println("7. Transfer Funds"); // Non-Idempotent
                System.out.println("q. Quit");

                System.out.print("Choice: ");
                String choice = sc.nextLine();
                if (choice.equalsIgnoreCase("q")) break;

                byte[] requestPayload = null;
                Duration d = Duration.ZERO;

                switch (choice) {
                    case "1": // Open Account: Op(4) + ReqID(4) + NameLen(4) + Name(n) + PwLen(4) + Pw(m) + Curr(4) + Bal(4)
                        System.out.print("Name: "); String name = sc.nextLine();
                        System.out.print("Password: "); String pw = sc.nextLine();

                        Currency curr = null;
                        while (curr == null) {
                            System.out.print("Currency (USD, JPY, or SGD): ");
                            String currInput = sc.nextLine().trim().toUpperCase();
                            try {
                                curr = Currency.valueOf(currInput);
                            } catch (IllegalArgumentException e) {
                                System.out.println("Invalid currency '" + currInput + "'. Please enter USD, JPY, or SGD.");
                            }
                        }

                        System.out.print("Initial Balance: "); float bal;
                        try {
                            bal = Float.parseFloat(sc.nextLine());
                            if (bal < 0) {
                                System.out.println("Negative account balance is impossible");
                                break;
                            }
                        } catch (NumberFormatException e) {
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
                            // e.printStackTrace();
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
                            // e.printStackTrace();
                            System.out.println("The account number you just typed in was not recognised. Please try again.");
                            break;
                        }
                        System.out.print("Name: "); String dName = sc.nextLine();
                        System.out.print("Password: "); String dPw = sc.nextLine();
                        Currency dw_curr = null;
                        while (dw_curr == null) {
                            System.out.print("Currency (USD, JPY, or SGD): ");
                            String dw_curr_str = sc.nextLine().trim().toUpperCase();
                            try {
                                dw_curr = Currency.valueOf(dw_curr_str);
                            } catch (IllegalArgumentException e) {
                                System.out.println("Invalid currency '" + dw_curr_str + "'. Please enter USD, JPY, or SGD.");
                            }
                        }
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

                        byte[] dNameBytes = dName.getBytes(StandardCharsets.UTF_8);
                        byte[] dPwBytes = dPw.getBytes(StandardCharsets.UTF_8);

                        ByteBuffer transBuf = ByteBuffer.allocate(28 + dPwBytes.length + dNameBytes.length);
                        transBuf.order(ByteOrder.BIG_ENDIAN);
                        transBuf.putInt(opCode);
                        transBuf.putInt(++requestId);
                        transBuf.putInt(dAcc);
                        transBuf.putInt(dNameBytes.length);
                        transBuf.put(dNameBytes);
                        transBuf.putInt(dPwBytes.length);
                        transBuf.put(dPwBytes);
                        transBuf.putFloat(amt);
                        transBuf.putInt(dw_curr.getCurrencyNumber());
                        requestPayload = transBuf.array();
                        break;
                    case "5":
                        System.out.print("Monitor Interval (Example usage - 1hrs 30mins 5secs): "); String interval = sc.nextLine();
                        long value = 0;
                        // no negative numbers
                        p = Pattern.compile("\\b(\\d+)\\s?(h|hr|hrs|hour|hours|min|mins|minute|minutes|m|s|sec|secs|second|seconds)", Pattern.CASE_INSENSITIVE);
                        m = p.matcher(interval);

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

                    case "6": // Check Balance: Op(4) + ReqID(4) + AccNum(4) + PwLen(4) + Pw(n)
                        System.out.print("Account Number: "); int bAcc;
                        try {
                            long unsigned_bAcc = Long.parseLong(sc.nextLine());
                            if ((unsigned_bAcc & 0x7fffffff00000000L) > 0 || unsigned_bAcc < 0) {
                                System.out.println("The account number you just typed in exceeded maximum bounds. Please try again.");
                                break;
                            }
                            bAcc = (int) unsigned_bAcc;
                        } catch (IllegalArgumentException e) {
                            System.out.println("The account number you just typed in was not recognised. Please try again.");
                            break;
                        }
                        System.out.print("Password: "); String bPw = sc.nextLine();

                        byte[] bPwBytes = bPw.getBytes(StandardCharsets.UTF_8);

                        ByteBuffer chkBuf = ByteBuffer.allocate(16 + bPwBytes.length);
                        chkBuf.order(ByteOrder.BIG_ENDIAN);
                        chkBuf.putInt(Constants.OP_CHECK_BALANCE);
                        chkBuf.putInt(++requestId);
                        chkBuf.putInt(bAcc);
                        chkBuf.putInt(bPwBytes.length);
                        chkBuf.put(bPwBytes);
                        requestPayload = chkBuf.array();
                        break;

                    case "7": // Transfer Funds: Op(4) + ReqID(4) + SrcAcc(4) + PwLen(4) + Pw(n) + DstAcc(4) + Amount(4)
                        System.out.print("Source Account Number: "); int tSrcAcc;
                        try {
                            long unsigned_tSrcAcc = Long.parseLong(sc.nextLine());
                            if ((unsigned_tSrcAcc & 0x7fffffff00000000L) > 0 || unsigned_tSrcAcc < 0) {
                                System.out.println("The account number exceeded maximum bounds. Please try again.");
                                break;
                            }
                            tSrcAcc = (int) unsigned_tSrcAcc;
                        } catch (NumberFormatException e) {
                            System.out.println("The account number was not recognised. Please try again.");
                            break;
                        }
                        System.out.print("Password: "); String tPw = sc.nextLine();
                        System.out.print("Destination Account Number: "); int tDstAcc;
                        try {
                            long unsigned_tDstAcc = Long.parseLong(sc.nextLine());
                            if ((unsigned_tDstAcc & 0x7fffffff00000000L) > 0 || unsigned_tDstAcc < 0) {
                                System.out.println("The account number exceeded maximum bounds. Please try again.");
                                break;
                            }
                            tDstAcc = (int) unsigned_tDstAcc;
                        } catch (NumberFormatException e) {
                            System.out.println("The account number was not recognised. Please try again.");
                            break;
                        }
                        System.out.print("Amount to Transfer: "); float tAmt;
                        try {
                            tAmt = Float.parseFloat(sc.nextLine());
                            if (tAmt <= 0) {
                                System.out.println("Amount must be positive. Please try again.");
                                break;
                            }
                        } catch (NumberFormatException e) {
                            System.out.println("The amount was not recognised. Please try again.");
                            break;
                        }

                        byte[] tPwBytes = tPw.getBytes(StandardCharsets.UTF_8);
                        ByteBuffer transferBuf = ByteBuffer.allocate(24 + tPwBytes.length);
                        transferBuf.order(ByteOrder.BIG_ENDIAN);
                        transferBuf.putInt(Constants.OP_TRANSFER);
                        transferBuf.putInt(++requestId);
                        transferBuf.putInt(tSrcAcc);
                        transferBuf.putInt(tPwBytes.length);
                        transferBuf.put(tPwBytes);
                        transferBuf.putInt(tDstAcc);
                        transferBuf.putFloat(tAmt);
                        requestPayload = transferBuf.array();
                        break;
                }
                
                if (requestPayload != null) {
                    // Use the new sendAndReceive for At-Least-Once reliability
                    byte[] response;
                    try {
                        response = client.sendAndReceive(requestPayload);
                    } catch (Exception e) {
                        System.out.println(e.getMessage());
                        continue;
                    }
                    
                    boolean start_clock = false;
                    long start_time, end_time;

                    ByteBuffer res_buf = ByteBuffer.wrap(response).order(ByteOrder.BIG_ENDIAN); // network order BIG_ENDIAN
                    int opcode = res_buf.getInt();
                    String response_str = presentData(res_buf, opcode, false);
                    
                    System.out.println("\n[SERVER RESPONSE]: " + response_str);
                    if (opcode == Constants.OP_MONITOR) {
                        byte[] data;
                        end_time = System.nanoTime() + d.toNanos();
                        client.set_timeout((int) Math.min(Integer.MAX_VALUE, Math.max(0L, d.toMillis())));
                        start_clock = true;
                        while ((data = client.monitor_receive()).length != 0) {
                            // prevent negative timeout
                            int timeout = (int) Math.min(Integer.MAX_VALUE, Math.max(0L, Duration.ofNanos(end_time - System.nanoTime()).toMillis()));
                            client.set_timeout(timeout);
                            ByteBuffer data_buf = ByteBuffer.wrap(data).order(ByteOrder.BIG_ENDIAN);
                            response_str = presentData(data_buf, data_buf.getInt(), true);
                            System.out.println("\n[MONITOR UPDATE]: " + response_str);
                        }
                    }

                    if (start_clock) {
                        client.reset_timeout();
                        start_clock = false;
                    }
                }
            }
            client.close();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private static String presentData(ByteBuffer res_buf, int opcode, boolean isMonitor) {
        long accNum;
        float balance, amount;

        byte[] reply_type = new byte[res_buf.getInt()];
        res_buf.get(reply_type);
        String reType = new String(reply_type, StandardCharsets.UTF_8).trim(), response_str = "";
        switch (opcode) {
            case Constants.OP_OPEN:
                if (reType.equals("SUCCESS")) {
                    accNum = res_buf.getInt() & 0xffffffffL; // treat as unsigned int
                    response_str = "ACCOUNT CREATION SUCCESSFUL, account number: " + accNum;
                }
                break;
            case Constants.OP_CLOSE:
                if (reType.equals("ERROR_ACCOUNT_NOT_FOUND")) {
                    byte[] cl_error = new byte[res_buf.getInt()];
                    res_buf.get(cl_error);
                    String cl_error_msg = new String(cl_error, StandardCharsets.UTF_8);
                    response_str = "ACCOUNT CLOSURE ERROR: " + cl_error_msg;
                } else if (reType.equals("SUCCESS")) {
                    byte[] cl_success = new byte[res_buf.getInt()];
                    res_buf.get(cl_success);
                    String cl_success_msg = new String(cl_success, StandardCharsets.UTF_8);
                    response_str = "ACCOUNT CLOSURE SUCCESSFUL: " + cl_success_msg;
                }
                break;
            case Constants.OP_DEPOSIT:
            case Constants.OP_WITHDRAW:
                String prefix, action;
                if (reType.equals("SUCCESS")) {
                    accNum = res_buf.getInt() & 0xffffffL;
                    amount = res_buf.getFloat();
                    Currency amount_curr = Currency.fromInt(res_buf.getInt());
                    balance = res_buf.getFloat();
                    Currency balance_curr = Currency.fromInt(res_buf.getInt());
                    prefix = (opcode == Constants.OP_DEPOSIT) ? "DEPOSIT SUCCESSFUL" : "WITHDRAW SUCCESSFUL";
                    action = (opcode == Constants.OP_DEPOSIT) ? "Deposited" : "Withdrawn";
                    response_str = String.format("%s: %s %.2f %s into account %d, new balance: %.2f %s", prefix, action, amount, amount_curr.name(), accNum, balance, balance_curr.name());
                } else if (reType.equals("ERROR_ACCOUNT_NOT_FOUND")) {
                    byte[] d_error = new byte[res_buf.getInt()];
                    res_buf.get(d_error);
                    String d_error_msg = new String(d_error, StandardCharsets.UTF_8);
                    prefix = (opcode == Constants.OP_DEPOSIT) ? "DEPOSIT ERROR: " : "WITHDRAW ERROR: ";
                    response_str = prefix + d_error_msg;
                } else if (reType.equals("ERROR_INSUFFICIENT_BALANCE")) {
                    byte[] w_error = new byte[res_buf.getInt()];
                    res_buf.get(w_error);
                    String w_error_msg = new String(w_error, StandardCharsets.UTF_8);
                    response_str = "WITHDRAW ERROR: " + w_error_msg;
                }
                break;
            case Constants.OP_MONITOR:
                byte[] ack = new byte[res_buf.getInt()];
                res_buf.get(ack);
                String ack_msg = new String(ack, StandardCharsets.UTF_8);
                response_str = "ACK: " + ack_msg;
                break;
            case Constants.OP_CHECK_BALANCE:
                if (reType.equals("SUCCESS")) {
                    accNum = res_buf.getInt() & 0xffffffffL;
                    balance = res_buf.getFloat();
                    response_str = String.format("CHECK BALANCE SUCCESSFUL: Account %d has a balance of %.2f", accNum, balance);
                } else if (reType.equals("ERROR_ACCOUNT_NOT_FOUND")) {
                    byte[] cb_error = new byte[res_buf.getInt()];
                    res_buf.get(cb_error);
                    String cb_error_msg = new String(cb_error, StandardCharsets.UTF_8);
                    response_str = "CHECK BALANCE ERROR: " + cb_error_msg;
                }
                break;
            case Constants.OP_TRANSFER:
                if (reType.equals("SUCCESS")) {
                    long srcAcc = res_buf.getInt() & 0xffffffffL;
                    long dstAcc = res_buf.getInt() & 0xffffffffL;
                    float srcAmt = res_buf.getFloat();
                    float dstAmt = res_buf.getFloat();
                    float newSrcBal = res_buf.getFloat();
                    float newDstBal = res_buf.getFloat();
                    float rate = res_buf.getFloat();
                    String[] currNames = {"USD", "JPY", "SGD"};
                    String srcCurr = currNames[res_buf.getInt() - 1];
                    String dstCurr = currNames[res_buf.getInt() - 1];
                    if (isMonitor) {
                        response_str = String.format(
                            "TRANSFER: Account %d sent %.2f %s -> Account %d received %.2f %s" +
                            " (1 %s = %.4f %s) | Account %d balance: %.2f %s, Account %d balance: %.2f %s",
                            srcAcc, srcAmt, srcCurr, dstAcc, dstAmt, dstCurr,
                            srcCurr, rate, dstCurr,
                            srcAcc, newSrcBal, srcCurr, dstAcc, newDstBal, dstCurr);
                    } else {
                        response_str = String.format(
                            "TRANSFER SUCCESSFUL: Sent %.2f %s from Account %d (new balance: %.2f %s)" +
                            " -> Credited %.2f %s to Account %d (new balance: %.2f %s)",
                            srcAmt, srcCurr, srcAcc, newSrcBal, srcCurr,
                            dstAmt, dstCurr, dstAcc, newDstBal, dstCurr);
                    }
                } else if (reType.equals("ERROR_ACCOUNT_NOT_FOUND")) {
                    byte[] t_err = new byte[res_buf.getInt()]; res_buf.get(t_err);
                    response_str = "TRANSFER ERROR: " + new String(t_err, StandardCharsets.UTF_8);
                } else if (reType.equals("ERROR_DEST_NOT_FOUND")) {
                    byte[] t_err = new byte[res_buf.getInt()]; res_buf.get(t_err);
                    response_str = "TRANSFER ERROR: " + new String(t_err, StandardCharsets.UTF_8);
                } else if (reType.equals("ERROR_SAME_ACCOUNT")) {
                    byte[] t_err = new byte[res_buf.getInt()]; res_buf.get(t_err);
                    response_str = "TRANSFER ERROR: " + new String(t_err, StandardCharsets.UTF_8);
                } else if (reType.equals("ERROR_INSUFFICIENT_BALANCE")) {
                    byte[] t_err = new byte[res_buf.getInt()]; res_buf.get(t_err);
                    response_str = "TRANSFER ERROR: " + new String(t_err, StandardCharsets.UTF_8);
                }
                break;
            default:
                byte[] gen_err = new byte[res_buf.getInt()]; res_buf.get(gen_err);
                response_str = "ERROR: " + new String(gen_err, StandardCharsets.UTF_8);
        }

        return response_str;
    }
}