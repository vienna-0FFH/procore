@{
    # TinyCC is kept outside the uCore image while its hosted cross-build is
    # being validated. The path is editable for another WSL distribution.
    TinyCcWslPath = '/home/vienna/tinycc-native-lf/tcc'
    QemuMemory = '128M'
    QemuSmp = 4
    BuildTimeoutSeconds = 180
    QemuTimeoutSeconds = 45
}
