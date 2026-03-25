import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;

public class ClientSocket {
    private DatagramSocket socket;
    private InetAddress serverAddress;
    private int serverPort = 8000;

    public ClientSocket() throws Exception {
        // We initialize the socket once. 
        // Using 0 lets the OS pick a free port so you don't get "Address in use" errors.
        this.socket = new DatagramSocket(); 
        this.serverAddress = InetAddress.getLoopbackAddress();
    }

    public void sendRequest(byte[] data) throws Exception {
        DatagramPacket packet = new DatagramPacket(data, data.length, serverAddress, Constants.PORT);
        socket.setSoTimeout(2000); // 2-second timeout for retransmission [cite: 105]
        socket.send(packet);
    }

    public String receiveResponse() throws Exception {
        byte[] buffer = new byte[255]; // Your original SIZE
        DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
        socket.receive(packet);
        
        return new String(packet.getData(), 0, packet.getLength(), StandardCharsets.UTF_8);
    }

    public void close() {
        if (socket != null) socket.close();
    }
}
