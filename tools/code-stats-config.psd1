@{
    # Source files included in the primary LOC report.
    CodeExtensions = @('.c', '.h', '.s', '.asm', '.inc', '.csrc')

    # Generated outputs and repository metadata are not source LOC. The
    # tracked target/native/compat directory remains included because it
    # contains build/linker source used by this project.
    ExcludedDirectories = @('.git', 'obj', 'target/native/bin',
                            'target/native/obj', 'target/native/disk0',
                            'target/native/ltp', 'target/native/tcc',
                            'target/native/tcc-src')

    # These paths identify code brought in from, or directly adapted from,
    # mature external projects.  The patterns are relative to the project
    # root and can be extended as more ports are added.
    PortedCategories = @{
        'TinyCC upstream source' = @('user/tcc/src/**')
        'MbedTLS upstream source' = @('user/mbedtls/src/**',
                                      'user/mbedtls/include/mbedtls/**')
        'C4 adapted source' = @('user/c4.c', 'user/c4demo.csrc')
        'LTP adapted source' = @('user/ltp_legacy.c')
    }
}
