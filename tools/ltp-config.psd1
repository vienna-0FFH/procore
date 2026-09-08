@{
    # Host-specific executable can be overridden with -QemuPath or UCORE_QEMU.
    QemuPath = 'E:\toolsE\qemu\qemu-system-i386.exe'
    QemuMemory = '128M'
    QemuSmp = 4
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
        'vfstest',
        'nettest',
        'affinitytest',
        'schedtest',
        'cowtest',
        'c4',
        'ltp_legacy',
        'elfgen'
    )
}
