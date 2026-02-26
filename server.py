import socket

# Configuration
TCP_IP = '0.0.0.0'  # Listen on all network interfaces
TCP_PORT = 4242     # Must match SERVER_PORT in your C code
BUFFER_SIZE = 1024

def start_server():
    # Create a TCP/IP socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        # Allow the port to be reused immediately after the script stops
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        # Bind the socket to the address and port
        s.bind((TCP_IP, TCP_PORT))
        s.listen(5)
        print(f"Server started on port {TCP_PORT}. Waiting for Pico W...")

        while True:
            # Accept a new connection from the Pico
            conn, addr = s.accept()
            with conn:
                print(f"Connection established from: {addr}")
                
                # Receive the data stream
                data = conn.recv(BUFFER_SIZE)
                if data:
                    # Decode the bytes into a string
                    message = data.decode('utf-8')
                    print(f"Received Message: {message}")
                    
                    # Optional: Send an acknowledgement back
                    conn.sendall(b"ACK from Server")

if __name__ == "__main__":
    start_server()

