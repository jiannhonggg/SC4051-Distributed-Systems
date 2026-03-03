import java.util.Scanner;

public class MainApp {
    public static void main(String[] args) {
        Scanner sc = new Scanner(System.in);
        try {
            ClientSocket client = new ClientSocket();
            boolean running = true;

            while (running) {
                System.out.println("\n--- Distributed Bank Menu ---");
                System.out.println("1. Open Account");
                System.out.println("2. Close Account");
                System.out.println("3. Deposit");
                System.out.println("4. Withdraw");
                System.out.println("5. Monitor");
                System.out.println("6. Transfer");
                System.out.println("7. Last Operation");
                System.out.println("8. Last Operation");

                System.out.print("Choice: ");
                
                String choice = sc.nextLine();
                String request = "";

                switch (choice) {
                    case "1": // Open Account
                        System.out.print("Name: "); String name = sc.nextLine();
                        System.out.print("Password: "); String pw = sc.nextLine();
                        System.out.print("Currency (USD/EUR): "); String curr = sc.nextLine();
                        System.out.print("Initial Balance: "); String bal = sc.nextLine();
                        // Format: OPEN|Name|Password|Currency|Balance
                        request = String.join(Constants.DELIMITER, Constants.OP_OPEN, name, pw, curr, bal);
                        break;

                    case "2": // Close Account
                        System.out.print("Name: "); String cName = sc.nextLine();
                        System.out.print("Account Number: "); String accNum = sc.nextLine();
                        System.out.print("Password: "); String cPw = sc.nextLine();
                        request = String.join(Constants.DELIMITER, Constants.OP_CLOSE, cName, accNum, cPw);
                        break;

                    case "3": // Deposit
                    case "4": // Withdraw
                        String op = choice.equals("3") ? Constants.OP_DEPOSIT : Constants.OP_WITHDRAW;
                        System.out.print("Account Number: "); String dAcc = sc.nextLine();
                        System.out.print("Password: "); String dPw = sc.nextLine();
                        System.out.print("Amount: "); String amt = sc.nextLine();
                        request = String.join(Constants.DELIMITER, op, dAcc, dPw, amt);
                        break;

                    case "5": // Monitor 
                        running = false;
                        continue;
                    case "6": // Transaction 
                        running = false; 
                        continue ;
                    case "7": // 
                        running = false; 
                        continue ;  
                    case "8": // 
                        running = false; 
                        continue ;  
                }

                if (!request.isEmpty()) {
                    client.sendRequest(request);
                    String response = client.receiveResponse();
                    System.out.println("\n[SERVER]: " + response);
                }
            }
            client.close();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}