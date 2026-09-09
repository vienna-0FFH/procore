import socket
import ssl
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18443
ROOT = 'tools/https-test-root'

context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.minimum_version = ssl.TLSVersion.TLSv1_2
context.maximum_version = ssl.TLSVersion.TLSv1_2
context.load_cert_chain(ROOT + '/server.crt', ROOT + '/server.key')

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('0.0.0.0', PORT))
    listener.listen(1)
    connection, _ = listener.accept()
    with context.wrap_socket(connection, server_side=True) as client:
        request = client.recv(4096)
        if request:
            client.sendall(
                b'HTTP/1.0 200 OK\r\n'
                b'Content-Length: 31\r\n'
                b'Connection: close\r\n\r\n'
                b'uCore HTTPS TLS test response\n')
