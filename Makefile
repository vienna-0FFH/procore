PROJ	:= 5
EMPTY	:=
SPACE	:= $(EMPTY) $(EMPTY)
SLASH	:= /

V       := @

# try to infer the correct GCCPREFX
ifndef GCCPREFIX
GCCPREFIX := $(shell if i386-ucore-elf-objdump -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
	then echo 'i386-ucore-elf-'; \
	elif objdump -i 2>&1 | grep 'elf32-i386' >/dev/null 2>&1; \
	then echo ''; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find an i386-ucore-elf version of GCC/binutils." 1>&2; \
	echo "*** Is the directory with i386-ucore-elf-gcc in your PATH?" 1>&2; \
	echo "*** If your i386-ucore-elf toolchain is installed with a command" 1>&2; \
	echo "*** prefix other than 'i386-ucore-elf-', set your GCCPREFIX" 1>&2; \
	echo "*** environment variable to that prefix and run 'make' again." 1>&2; \
	echo "*** To turn off this error, run 'gmake GCCPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

# try to infer the correct QEMU
ifndef QEMU
QEMU := $(shell if which qemu-system-i386 > /dev/null; \
	then echo 'qemu-system-i386'; exit; \
	elif which i386-ucore-elf-qemu > /dev/null; \
	then echo 'i386-ucore-elf-qemu'; exit; \
	else \
	echo "***" 1>&2; \
	echo "*** Error: Couldn't find a working QEMU executable." 1>&2; \
	echo "*** Is the directory containing the qemu binary in your PATH" 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

# eliminate default suffix rules
.SUFFIXES: .c .S .h

# delete target files if there is an error (or make is interrupted)
.DELETE_ON_ERROR:

# define compiler and flags

HOSTCC		:= gcc
HOSTCFLAGS	:= -g -Wall -O2 -D_FILE_OFFSET_BITS=64

GDB		:= $(GCCPREFIX)gdb

CC		:= $(GCCPREFIX)gcc
CFLAGS	:= -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc $(DEFS)
# Native wrappers provide a fixed command-line CFLAGS value.  Keep the
# traditional DEFS+=... override visible to kernel compilation in that mode.
override CFLAGS += $(DEFS)
CFLAGS	+= $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)
CTYPE	:= c S

LD      := $(GCCPREFIX)ld
LDFLAGS	:= -m $(shell $(LD) -V | grep elf_i386 2>/dev/null)
LDFLAGS	+= -nostdlib

OBJCOPY := $(GCCPREFIX)objcopy
OBJDUMP := $(GCCPREFIX)objdump

COPY	:= cp
MKDIR   := mkdir -p
MV		:= mv
RM		:= rm -f
AWK		:= awk
SED		:= sed
SH		:= sh
TR		:= tr
TOUCH	:= touch -c

OBJDIR	:= obj
BINDIR	:= bin

ALLOBJS	:=
ALLDEPS	:=
TARGETS	:=

USER_PREFIX	:= __user_

include tools/function.mk

listf_cc = $(call listf,$(1),$(CTYPE))

# for cc
# User headers must precede the kernel include directories.  Both layers have
# a file.h, and letting -Ikern/fs win silently turns user programs' open/read
# declarations into kernel-only structure definitions.  Keep kernel ordering
# unchanged while making the user ABI deterministic and warning-free.
add_files_cc = $(call add_files,$(1),$(CC),$(if $(filter ulibs uprog,$(2)),$(3) $(CFLAGS),$(CFLAGS) $(3)),$(2),$(4))
create_target_cc = $(call create_target,$(1),$(2),$(3),$(CC),$(CFLAGS))

# for hostcc
add_files_host = $(call add_files,$(1),$(HOSTCC),$(HOSTCFLAGS),$(2),$(3))
create_target_host = $(call create_target,$(1),$(2),$(3),$(HOSTCC),$(HOSTCFLAGS))

cgtype = $(patsubst %.$(2),%.$(3),$(1))
objfile = $(call toobj,$(1))
asmfile = $(call cgtype,$(call toobj,$(1)),o,asm)
outfile = $(call cgtype,$(call toobj,$(1)),o,out)
symfile = $(call cgtype,$(call toobj,$(1)),o,sym)
filename = $(basename $(notdir $(1)))
ubinfile = $(call outfile,$(addprefix $(USER_PREFIX),$(call filename,$(1))))

# for match pattern
match = $(shell echo $(2) | $(AWK) '{for(i=1;i<=NF;i++){if(match("$(1)","^"$$(i)"$$")){exit 1;}}}'; echo $$?)

# >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
# include kernel/user

INCLUDE	+= libs/

CFLAGS	+= $(addprefix -I,$(INCLUDE))

LIBDIR	+= libs

$(call add_files_cc,$(call listf_cc,$(LIBDIR)),libs,)

# -------------------------------------------------------------------
# user programs

UINCLUDE	+= user/include/ \
			   user/libs/

USRCDIR		+= user

ULIBDIR		+= user/libs

UCFLAGS		+= $(addprefix -I,$(UINCLUDE))
USER_BINS	:=

$(call add_files_cc,$(call listf_cc,$(ULIBDIR)),ulibs,$(UCFLAGS))
TCC_USER_SOURCE := user/tcc.c
USER_SOURCE_FILES := $(filter-out $(TCC_USER_SOURCE),$(call listf_cc,$(USRCDIR)))
$(call add_files_cc,$(USER_SOURCE_FILES),uprog,$(UCFLAGS))

UOBJS	:= $(call read_packet,ulibs libs)

# TinyCC is one large translation unit and needs its own include path.  Keep
# it outside USER_BINS so the ordinary user-program pattern does not try to
# link it a second time.
TCC_OBJ := $(OBJDIR)/tcc/tcc.o
TCC_LIBTCC1_OBJ := $(OBJDIR)/tcc/libtcc1.o
TCC_BIN := $(BINDIR)/tcc

$(OBJDIR)/tcc:
	@$(MKDIR) $@

$(TCC_OBJ): $(TCC_USER_SOURCE) | $(OBJDIR)/tcc
	@echo + cc $< TinyCC
	$(V)$(CC) -Iuser/tcc/src -Iuser/tcc/include -Iuser/libs -Iuser/include -Ilibs $(CFLAGS) $(UCFLAGS) -c $< -o $@

$(TCC_LIBTCC1_OBJ): user/tcc/src/libtcc1.c | $(OBJDIR)/tcc
	@echo + cc $< libtcc1
	$(V)$(CC) -fheinous-gnu-extensions -Iuser/tcc/src -Iuser/tcc/include -Iuser/libs -Iuser/include -Ilibs $(CFLAGS) $(UCFLAGS) -c $< -o $@

$(TCC_BIN): $(TCC_OBJ) $(TCC_LIBTCC1_OBJ) $(UOBJS) target/native/compat/user.ld | $(BINDIR)
	@echo + ld $@ TinyCC
	$(V)$(LD) $(LDFLAGS) -T target/native/compat/user.ld -o $@ $(UOBJS) $(TCC_LIBTCC1_OBJ) $(TCC_OBJ)

TARGETS += $(TCC_BIN)

define uprog_ld
__user_bin__ := $$(call ubinfile,$(1))
USER_BINS += $$(__user_bin__)
$$(__user_bin__): tools/user.ld
$$(__user_bin__): $$(UOBJS)
$$(__user_bin__): $(1) | $$$$(dir $$$$@)
	$(V)$(LD) $(LDFLAGS) -T tools/user.ld -o $$@ $$(UOBJS) $(1)
	@$(OBJDUMP) -S $$@ > $$(call cgtype,$$<,o,asm)
	@$(OBJDUMP) -t $$@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$$$/d' > $$(call cgtype,$$<,o,sym)
endef

$(foreach p,$(call read_packet,uprog),$(eval $(call uprog_ld,$(p))))

# -------------------------------------------------------------------
# kernel

KINCLUDE	+= kern/debug/ \
			   kern/driver/ \
			   kern/net/ \
			   kern/smp/ \
			   kern/virt/ \
			   kern/trap/ \
			   kern/mm/ \
			   kern/libs/ \
			   kern/sync/ \
			   kern/fs/    \
			   kern/process/ \
			   kern/schedule/ \
			   kern/syscall/  \
			   kern/fs/swap/ \
			   kern/fs/vfs/ \
			   kern/fs/devs/ \
			   kern/fs/sfs/ 


KSRCDIR		+= kern/init \
			   kern/libs \
			   kern/debug \
			   kern/driver \
			   kern/net \
			   kern/smp \
			   kern/virt \
			   kern/trap \
			   kern/mm \
			   kern/sync \
			   kern/fs    \
			   kern/process \
			   kern/schedule \
			   kern/syscall  \
			   kern/fs/swap \
			   kern/fs/vfs \
			   kern/fs/devs \
			   kern/fs/sfs

KCFLAGS		+= $(addprefix -I,$(KINCLUDE))
# Optional platform overrides, e.g. SMP_DEFS+=-DSMP_MAX_CPUS=4.
KCFLAGS		+= $(SMP_DEFS)

$(call add_files_cc,$(call listf_cc,$(KSRCDIR)),kernel,$(KCFLAGS))

KOBJS	= $(call read_packet,kernel libs)

# create kernel target
kernel = $(call totarget,kernel)

$(kernel): tools/kernel.ld

$(kernel): $(KOBJS)
	@echo + ld $@
	$(V)$(LD) $(LDFLAGS) -T tools/kernel.ld -o $@ $(KOBJS)
	@$(OBJDUMP) -S $@ > $(call asmfile,kernel)
	@$(OBJDUMP) -t $@ | $(SED) '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(call symfile,kernel)

$(call create_target,kernel)

# -------------------------------------------------------------------

# create bootblock
bootfiles = $(call listf_cc,boot)
$(foreach f,$(bootfiles),$(call cc_compile,$(f),$(CC),$(CFLAGS) -Os -nostdinc))

bootblock = $(call totarget,bootblock)

$(bootblock): $(call toobj,boot/bootasm.S) $(call toobj,$(bootfiles)) | $(call totarget,sign)
	@echo + ld $@
	$(V)$(LD) $(LDFLAGS) -N -T tools/boot.ld $^ -o $(call toobj,bootblock)
	@$(OBJDUMP) -S $(call objfile,bootblock) > $(call asmfile,bootblock)
	@$(OBJCOPY) -S -O binary $(call objfile,bootblock) $(call outfile,bootblock)
	@$(call totarget,sign) $(call outfile,bootblock) $(bootblock)

$(call create_target,bootblock)

# -------------------------------------------------------------------

# create 'sign' tools
$(call add_files_host,tools/sign.c,sign,sign)
$(call create_target_host,sign,sign)

# -------------------------------------------------------------------
# create 'mksfs' tools
$(call add_files_host,tools/mksfs.c,mksfs,mksfs)
$(call create_target_host,mksfs,mksfs)

# -------------------------------------------------------------------
# create ucore.img
UCOREIMG	:= $(call totarget,ucore.img)

$(UCOREIMG): $(kernel) $(bootblock)
	$(V)dd if=/dev/zero of=$@ count=10000
	$(V)dd if=$(bootblock) of=$@ conv=notrunc
	$(V)dd if=$(kernel) of=$@ seek=1 conv=notrunc

$(call create_target,ucore.img)

# -------------------------------------------------------------------

# create swap.img
SWAPIMG		:= $(call totarget,swap.img)

$(SWAPIMG):
	$(V)dd if=/dev/zero of=$@ bs=1M count=128

$(call create_target,swap.img)

# -------------------------------------------------------------------
# create sfs.img
SFSIMG		:= $(call totarget,sfs.img)
SFSBINS		:=
SFSROOT		:= disk0

# Runtime objects and headers consumed by TinyCC after uCore boots.
TCC_SFS_BIN := $(SFSROOT)/bin/tcc
TCC_RUNTIME_OBJS := $(UOBJS) $(TCC_LIBTCC1_OBJ)
TCC_RUNTIME_TARGETS := $(addprefix $(SFSROOT)/tcc/lib/,$(notdir $(TCC_RUNTIME_OBJS)))
TCC_HEADER_SOURCES := $(wildcard user/tcc/include/*.h user/tcc/include/sys/*.h)
TCC_HEADER_TARGETS := $(addprefix $(SFSROOT)/tcc/include/,$(patsubst user/tcc/include/%,%,$(TCC_HEADER_SOURCES)))
TCC_UCORE_HEADER_SOURCES := $(wildcard user/libs/*.h libs/*.h)
TCC_UCORE_HEADER_TARGETS := $(addprefix $(SFSROOT)/tcc/ucore/,$(notdir $(TCC_UCORE_HEADER_SOURCES)))
TCC_ASSET_DIRS := $(sort $(patsubst %/,%,$(dir $(TCC_SFS_BIN) $(TCC_RUNTIME_TARGETS) $(TCC_HEADER_TARGETS) $(TCC_UCORE_HEADER_TARGETS))))

$(TCC_ASSET_DIRS):
	@$(MKDIR) $@

$(TCC_SFS_BIN): $(TCC_BIN) | $(SFSROOT)/bin
	@$(COPY) $< $@
SFSBINS += $(TCC_SFS_BIN)

define tcc_runtime_copy
$(SFSROOT)/tcc/lib/$(notdir $(1)): $(1) | $(SFSROOT)/tcc/lib
	@$(COPY) $$< $$@
endef
$(foreach p,$(TCC_RUNTIME_OBJS),$(eval $(call tcc_runtime_copy,$(p))))
SFSBINS += $(TCC_RUNTIME_TARGETS)

define tcc_header_copy
$(SFSROOT)/tcc/include/$(patsubst user/tcc/include/%,%,$(1)): $(1) | $(TCC_ASSET_DIRS)
	@$(COPY) $$< $$@
endef
$(foreach p,$(TCC_HEADER_SOURCES),$(eval $(call tcc_header_copy,$(p))))
SFSBINS += $(TCC_HEADER_TARGETS)

define tcc_ucore_header_copy
$(SFSROOT)/tcc/ucore/$(notdir $(1)): $(1) | $(SFSROOT)/tcc/ucore
	@$(COPY) $$< $$@
endef
$(foreach p,$(TCC_UCORE_HEADER_SOURCES),$(eval $(call tcc_ucore_header_copy,$(p))))
SFSBINS += $(TCC_UCORE_HEADER_TARGETS)

# Source consumed by the in-OS c4 compiler test. Keep the source extension
# outside CTYPE so it is copied as data, not linked as a second user program.
C4_SOURCE		?= user/c4demo.csrc
C4_SOURCE_NAME	?= c4demo.c
C4_SFS_SOURCE	:= $(SFSROOT)$(SLASH)$(C4_SOURCE_NAME)
TCC_DEMO_SOURCE	?= user/tccdemo.csrc
TCC_DEMO_SOURCE_NAME ?= tccdemo.c
TCC_SFS_DEMO_SOURCE := $(SFSROOT)$(SLASH)$(TCC_DEMO_SOURCE_NAME)
TCC_ELF_NAME	?= tcc-program
TCC_SFS_ELF	:= $(SFSROOT)$(SLASH)$(TCC_ELF_NAME)

define fscopy
__fs_bin__ := $(2)$(SLASH)$(patsubst $(USER_PREFIX)%,%,$(basename $(notdir $(1))))
SFSBINS += $$(__fs_bin__)
$$(__fs_bin__): $(1) | $$$$(dir $@)
	@$(COPY) $$< $$@
endef

$(foreach p,$(USER_BINS),$(eval $(call fscopy,$(p),$(SFSROOT)$(SLASH))))

$(C4_SFS_SOURCE): $(C4_SOURCE) | $(SFSROOT)
	@$(COPY) $< $@

SFSBINS += $(C4_SFS_SOURCE)

$(TCC_SFS_DEMO_SOURCE): $(TCC_DEMO_SOURCE) | $(SFSROOT)
	@$(COPY) $< $@

SFSBINS += $(TCC_SFS_DEMO_SOURCE)

ifneq ($(strip $(TCC_ELF)),)
$(TCC_SFS_ELF): $(TCC_ELF) | $(SFSROOT)
	@$(COPY) $< $@

SFSBINS += $(TCC_SFS_ELF)
endif

$(SFSROOT):
	$(V)$(MKDIR) $@

$(SFSIMG): $(SFSROOT) $(SFSBINS) | $(call totarget,mksfs)
	$(V)dd if=/dev/zero of=$@ bs=1M count=128
	@$(call totarget,mksfs) $@ $(SFSROOT)

$(call create_target,sfs.img)


# >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

$(call finish_all)

IGNORE_ALLDEPS	= clean \
				  dist-clean \
				  grade \
				  touch \
				  print-.+ \
				  run-.+ \
				  build-.+ \
				  sh-.+ \
				  script-.+ \
				  handin

ifeq ($(call match,$(MAKECMDGOALS),$(IGNORE_ALLDEPS)),0)
-include $(ALLDEPS)
endif

# files for grade script

TARGETS: $(TARGETS)

.DEFAULT_GOAL := TARGETS

QEMUOPTS = -hda $(UCOREIMG) -drive file=$(SWAPIMG),media=disk,cache=writeback -drive file=$(SFSIMG),media=disk,cache=writeback 

.PHONY: qemu qemu-nox debug debug-nox monitor
qemu-mon: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(QEMU) -monitor stdio $(QEMUOPTS) -serial null
qemu: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(QEMU) -serial stdio $(QEMUOPTS) -parallel null
#	$(V)$(QEMU) -parallel stdio $(QEMUOPTS) -serial null

qemu-nox: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(QEMU) -serial mon:stdio $(QEMUOPTS) -nographic

monitor: $(UCOREIMG) $(SWAPING) $(SFSIMG)
	$(V)$(QEMU) -monitor stdio $(QEMUOPTS) -serial null

TERMINAL := gnome-terminal

dbg4ec: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(QEMU) -S -s -parallel stdio $(QEMUOPTS) -serial null

debug: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(QEMU) -S -s -parallel stdio $(QEMUOPTS) -serial null &
	$(V)sleep 2
	$(V)$(TERMINAL) -e "$(GDB) -q -x tools/gdbinit"

debug-nox: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(QEMU) -S -s -serial mon:stdio $(QEMUOPTS) -nographic &
	$(V)sleep 2
	$(V)$(TERMINAL) -e "$(GDB) -q -x tools/gdbinit"

RUN_PREFIX	:= _binary_$(OBJDIR)_$(USER_PREFIX)
MAKEOPTS	:= --quiet --no-print-directory

run-%: build-%
	$(V)$(QEMU) -parallel stdio $(QEMUOPTS) -serial null

sh-%: script-%
	$(V)$(QEMU) -parallel stdio $(QEMUOPTS) -serial null

run-nox-%: build-%
	$(V)$(QEMU) -serial mon:stdio $(QEMUOPTS) -nographic

build-%: touch
	$(V)$(MAKE) $(MAKEOPTS) "DEFS+=-DTEST=$*" 

script-%: touch
	$(V)$(MAKE) $(MAKEOPTS) "DEFS+=-DTEST=sh -DTESTSCRIPT=/script/$*"

.PHONY: grade touch buildfs

GRADE_GDB_IN	:= .gdb.in
GRADE_QEMU_OUT	:= .qemu.out
HANDIN			:= proj$(PROJ)-handin.tar.gz

TOUCH_FILES		:= kern/process/proc.c

MAKEOPTS		:= --quiet --no-print-directory

grade:
	$(V)$(MAKE) $(MAKEOPTS) clean
	$(V)$(SH) tools/grade.sh

touch:
	$(V)$(foreach f,$(TOUCH_FILES),$(TOUCH) $(f))

print-%:
	@echo $($(shell echo $(patsubst print-%,%,$@) | $(TR) [a-z] [A-Z]))

.PHONY: clean dist-clean handin packall
clean:
	$(V)$(RM) $(GRADE_GDB_IN) $(GRADE_QEMU_OUT)  $(SFSBINS)
	-$(RM) -r $(OBJDIR) $(BINDIR)

dist-clean: clean
	-$(RM) $(HANDIN)

handin: packall
	@echo Please visit http://learn.tsinghua.edu.cn and upload $(HANDIN). Thanks!

packall: clean
	@$(RM) -f $(HANDIN)
	@tar -czf $(HANDIN) `find . -type f -o -type d | grep -v '^\.*$$' | grep -vF '$(HANDIN)'`
