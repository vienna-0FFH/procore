#!/usr/bin/env bash

set -e

export HOME=/tmp
export PATH="/e/project_learning/ucore/ucore-master/labcodes_answer/lab8_result/target/native/compat:/usr/bin:/bin:/c/Program Files (x86)/Android/AndroidNDK/android-ndk-r23c/toolchains/llvm/prebuilt/windows-x86_64/bin:/e/toolsE/qemu"

cd /e/project_learning/ucore/ucore-master/labcodes_answer/lab8_result

/usr/bin/make --no-print-directory -B -j2 \
    SHELL=/usr/bin/sh \
    GCCPREFIX=ucore- \
    QEMU=qemu-system-i386 \
    HOSTCC=gcc \
    CC=clang \
    LD=ld.lld \
    OBJCOPY=llvm-objcopy \
    OBJDUMP=llvm-objdump \
    CFLAGS="--target=i386-unknown-elf -std=gnu89 -fno-builtin -Wall -ggdb -m32 -mno-sse -mno-sse2 -mno-mmx -mfpmath=387 -nostdinc -Ilibs -Ikern/debug -Ikern/driver -Ikern/trap -Ikern/mm -Ikern/libs -Ikern/sync -Ikern/fs -Ikern/process -Ikern/schedule -Ikern/syscall -Ikern/fs/swap -Ikern/fs/vfs -Ikern/fs/devs -Ikern/fs/sfs -include target/native/compat/init-prototypes.h" \
    UCFLAGS="-g0 -m32 -mno-sse -mno-sse2 -mno-mmx -mfpmath=387 -Iuser/include -Iuser/libs" \
    HOSTCFLAGS="-std=gnu17 -g -Wall -O2 -D_FILE_OFFSET_BITS=64" \
    LDFLAGS="-m elf_i386 -nostdlib" \
    OBJDIR=target/native/obj \
    BINDIR=target/native/bin \
    SFSROOT=target/native/disk0 \
    "$@"
