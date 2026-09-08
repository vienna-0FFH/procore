/*
 * uCore's TinyCC configuration.
 *
 * This compiler runs in a freestanding i386 user process, so it must not
 * inherit Linux's CRT, dynamic-loader or host include paths from configure.
 */
#ifndef UCORE_TCC_CONFIG_H
#define UCORE_TCC_CONFIG_H

#define TCC_VERSION "0.9.28rc-ucore"
#define TCC_TARGET_I386 1
#define UCORE_TCC 1
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCC_SEMLOCK 0
#define CONFIG_TCC_BACKTRACE 0
#define CONFIG_TCC_BCHECK 0
#ifndef UCORE_TCC_ROOT
#define UCORE_TCC_ROOT "/tcc"
#endif
#define CONFIG_TCCDIR UCORE_TCC_ROOT
#define CONFIG_TCC_SYSINCLUDEPATHS UCORE_TCC_ROOT "/include"
#define CONFIG_TCC_LIBPATHS UCORE_TCC_ROOT "/lib"
#define CONFIG_TCC_CRTPREFIX UCORE_TCC_ROOT "/lib"
#define CONFIG_TCC_ELFINTERP ""
#define UCORE_TCC_UCORE_INCLUDE UCORE_TCC_ROOT "/ucore"
#define UCORE_TCC_RUNTIME_DIR UCORE_TCC_ROOT "/lib"

#endif
