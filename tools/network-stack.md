# Network Stack

The current network path is layered so each stage can be tested independently:

| Layer | Status | Test |
| --- | --- | --- |
| e1000 DMA and ARP | implemented | `e1000test`, `netexternal` |
| IPv4 and UDP loopback | implemented | `nettest` |
| UDP `connect`/`send`/`recv` and readiness | implemented | `netconnecttest`, `polltest` |
| User-space DNS A resolver | implemented | `dnstest` |
| Active TCP client | implemented | `httpget` |
| TCP send window, slow start, congestion avoidance, duplicate-ACK recovery | implemented | `tcpwindowtest` |
| TCP listen/accept | planned | not exposed yet |
| TLS 1.2 client with X.509 verification | implemented for bundled test CA | `httpsget` |
| Public-root HTTPS trust store and clock policy | planned | external sites need bundled roots and valid time |

The TCP implementation follows the wire-level behavior used by Linux and
ReactOS (network-order headers, three-way handshake, sequence/acknowledgement
validation, FIN/EOF, and TCP checksum handling), but is deliberately scoped to
an active client until the shared connection table and listener backlog exist.
It supports a bounded send queue, advertised receive window, congestion
window, slow start, congestion avoidance, duplicate-ACK fast retransmit, and
bounded SYN/data retransmission controlled by `NET_TCP_RETRY_TICKS` and
`NET_TCP_RETRY_LIMIT`. A listener backlog and full FIN state machine remain
separate milestones.

## Host HTTP Test

The Windows-native runner can start a local Python HTTP server, attach QEMU's
user-mode network, and execute the in-OS client:

```powershell
& '.\tools\run-ltp.ps1' -Tests httpget -QemuUserNet -QemuHostHttp
```

The guest connects to `10.0.2.2:18080`, sends an HTTP/1.0 request, and checks
that a response header and body arrive through the kernel TCP path. The test
server is started from `tools/http-test-root/index.html` and is terminated by
the runner after QEMU exits.

For a real name-based request from the shell, the image also contains
`/webget`: `/webget example.com /`. It resolves the A record through the
QEMU DNS proxy and then uses TCP port 80. The bundled `/httpsget` test uses
the same TCP path with a locally trusted TLS 1.2 certificate; public-root
verification and a valid wall clock are separate configuration work.
