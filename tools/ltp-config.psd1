@{
    # Host-specific executable can be overridden with -QemuPath or UCORE_QEMU.
    QemuPath = 'E:\toolsE\qemu\qemu-system-i386.exe'
    QemuMemory = '128M'
    QemuSmp = 4
    QemuUserNet = $false
    QemuHostUdpPort = 19100
    QemuGuestUdpPort = 9100
    QemuHostHttp = $false
    QemuHostHttpPort = 18080
    QemuHostTcpEcho = $false
    QemuHostTcpEchoPort = 18081
    BuildTimeoutSeconds = 180
    QemuTimeoutSeconds = 45

    # Keep this list intentionally small and deterministic. Add a test only
    # after it emits the user-test-result marker and has a bounded runtime.
    Tests = @(
        'hello',
        'chdirtest',
        'clonetest',
        'mmaptest',
        'fdsharetest',
        'pipetest',
        'polltest',
        'vfstest',
        'nettest',
        'netconnecttest',
        'tcpwindowtest',
        'affinitytest',
        'schedtest',
        'cowtest',
        'c4',
        'tcc_run',
        'ltp_legacy',
        'elfgen'
    )
}
