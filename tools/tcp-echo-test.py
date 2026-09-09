import socket
import sys

HOST = '0.0.0.0'
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18080

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((HOST, PORT))
    listener.listen(1)
    connection, _ = listener.accept()
    print('accepted', flush=True)
    with connection:
        while True:
            data = connection.recv(4096)
            if not data:
                break
            print('echo', len(data), flush=True)
            connection.sendall(data)
