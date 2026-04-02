import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;

public class ClientSocket {
    private DatagramSocket socket;
    private InetAddress serverAddress;
    private int serverPort;
    private static final int TIMEOUT = 2000; // 2 seconds
    private static final int MAX_TRIES = 5;

    public ClientSocket(int port, String server_ip, int server_port) throws Exception {
        this.socket = new DatagramSocket(port);
        // this.socket = new DatagramSocket();
        
        // 1. SET SERVER IP:
        // Using "127.0.0.1" for testing on own laptop.
        // Change this to the actual Server IP (e.g., "10.96.x.x") in the NTU Lab.
        this.serverAddress = InetAddress.getByName(server_ip);
        this.serverPort = server_port;
        // this.serverAddress = InetAddress.getLoopbackAddress();

        // 2. SET TIMEOUT:
        // This is crucial for At-Least-Once semantics so the client doesn't hang forever.
        this.socket.setSoTimeout(TIMEOUT);
    }

    /**
     * Sends a request and waits for a response.
     * If no response is received within 2 seconds, it re-transmits the data (At-Least-Once).
     */
    public byte[] sendAndReceive(byte[] data) throws Exception {
        DatagramPacket sendPacket = new DatagramPacket(data, data.length, serverAddress, serverPort);
        
        byte[] buffer = new byte[1024]; 
        DatagramPacket receivePacket = new DatagramPacket(buffer, buffer.length);

        int tries = 0;
        while (tries < MAX_TRIES) {
            try {
                // Send the marshalled binary data
                socket.send(sendPacket);
                
                // Attempt to receive the reply
                socket.receive(receivePacket);
                
                // If successful, return the server's message as a String
                return receivePacket.getData();
                
            } catch (SocketTimeoutException e) {
                // This block handles lost Request or Reply packets
                tries++;
                System.out.println("[TIMEOUT] No response. Retrying (" + tries + "/" + MAX_TRIES + ")...");
            }
        }
        
        throw new Exception("Error: Server unreachable after " + MAX_TRIES + " attempts.");
    }

    /**
     * Function specifically for the monitor service to repeatedly receive updates
     * If monitor interval has expired, in which SO_TIMEOUT is set close to zero. Exception will be caught
     * and a byte array of 0 length is returned.
     */
    public byte[] monitor_receive() throws Exception {
        byte[] buffer = new byte[1024]; 
        DatagramPacket receivePacket = new DatagramPacket(buffer, buffer.length);
        try {
            socket.receive(receivePacket);
            return receivePacket.getData();
        } catch (SocketTimeoutException e) {
            return new byte[0];
        }
    }

    public void set_timeout(int interval) throws Exception{
        this.socket.setSoTimeout(interval);
    }

    // resets the timeout after monitor interval has expired back to 2s
    public void reset_timeout() throws Exception{
        this.socket.setSoTimeout(TIMEOUT);
    }

    public void close() {
        if (socket != null) {
            socket.close();
        }
    }
}