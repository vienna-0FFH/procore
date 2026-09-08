# Network Stack

The current network path is layered so each stage can be tested independently:

| Layer | Status | Test |
| --- | --- | --- |
| e1000 DMA and ARP | implemented | `e1000test`, `netexternal` |
| IPv4 and UDP loopback | implemented | `nettest` |
| UDP `connect`/`send`/`recv` and readiness | implemented | `netconnecttest`, `polltest` |
| Active TCP client | implemented | `httpget` |
| TCP listen/accept, retransmission, congestion control | planned | not exposed yet |
| TLS/HTTPS | planned | requires a TLS library and certificate/time policy |

The TCP implementation follows the wire-level behavior used by Linux and
ReactOS (network-order headers, three-way handshake, sequence/acknowledgement
validation, FIN/EOF, and TCP checksum handling), but is deliberately scoped to
an active client until the shared connection table and listener backlog exist.
It supports one in-flight segment per socket and the configured MSS; robust
retransmission and congestion control remain separate milestones.

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
