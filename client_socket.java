import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

class client_socket {
    public static void main(String[] args) {
        DatagramSocket sock = null;
        try {
            // Equivalent to creating a socket and binding it to a port. Socket will be bound to a wildcard address by default
            sock = new DatagramSocket(8080);
            
            int serverPort = 8000;
            InetAddress serverAddress = InetAddress.getLoopbackAddress();
            
            // Send a message to the server. Note that endianness does not affect array element ordering
            // char arrays especially are completely unaffected
            String message = "Hello, Server!";
            byte[] send_buffer = message.getBytes(StandardCharsets.UTF_8); // no need to handle encodings
            DatagramPacket request = new DatagramPacket(send_buffer, send_buffer.length, serverAddress, serverPort);
            sock.send(request);

            // Prepare to receive a response from the server
            final int SIZE = 255;
            byte[] recv_buffer = new byte[SIZE];
            DatagramPacket reply = new DatagramPacket(recv_buffer, recv_buffer.length);
            sock.receive(reply);
            
            // Message received. Hello from server!
            System.out.println(reply.getAddress().toString()); // 127.0.0.1
            System.out.println(reply.getPort()); // 8000
        } catch (Exception e) {
            e.printStackTrace();
            if (sock != null)
                sock.close();
        } finally {
            if (sock != null)
                sock.close();
        }
    }
}