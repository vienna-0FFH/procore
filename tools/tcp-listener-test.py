import socket
import sys
import time


PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18082
PAYLOAD = b"ucore-listener"
deadline = time.time() + 20

while time.time() < deadline:
    try:
        with socket.create_connection(("127.0.0.1", PORT), timeout=2) as client:
            print("TCP listener connection established", flush=True)
            client.settimeout(5)
            client.sendall(PAYLOAD)
            received = b""
            while len(received) < len(PAYLOAD):
                part = client.recv(len(PAYLOAD) - len(received))
                if not part:
                    raise RuntimeError("guest closed before echo completed")
                received += part
            if received != PAYLOAD:
                raise RuntimeError(f"unexpected echo: {received!r}")
            print("TCP listener echo received", flush=True)
            if client.recv(1) != b"":
                raise RuntimeError("guest did not close the accepted stream")
            print("TCP listener probe pass", flush=True)
            raise SystemExit(0)
    except (ConnectionRefusedError, TimeoutError, OSError):
        time.sleep(0.25)

print("guest TCP listener did not become ready", file=sys.stderr, flush=True)
raise SystemExit(1)
