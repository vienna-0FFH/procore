/*
 * Minimal native backend probe for uCore.
 *
 * This is deliberately a small ELF32 writer, not a general-purpose linker:
 * it emits a fixed i386 program which calls SYS_putc and SYS_exit through the
 * uCore int 0x80 ABI. The generated file is then loaded by the normal exec
 * path, exercising the ELF contract a future C compiler backend must follow.
 */
#include <defs.h>
#include <error.h>
#include <elf.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>
#include <file.h>
#include <unistd.h>

#define IMAGE_CAPACITY 8192
#define CODE_OFFSET 0x100
#define DATA_OFFSET 0x1000
#define CODE_VA 0x00800020
#define DATA_VA 0x00900000
#define DATA_SIZE 1

static uint8_t image[IMAGE_CAPACITY];

static void
put32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static size_t
emit_syscall(uint8_t *code, size_t offset, uint32_t number, uint32_t arg0) {
    /* mov edx,arg0; mov eax,number; int 0x80 */
    code[offset++] = 0xBA;
    put32(code + offset, arg0);
    offset += 4;
    code[offset++] = 0xB8;
    put32(code + offset, number);
    offset += 4;
    code[offset++] = 0xCD;
    code[offset++] = 0x80;
    return offset;
}

static int
build_image(size_t *image_size_store) {
    static const char message[] = "native ELF pass: 42\n";
    struct elfhdr elf;
    struct proghdr ph[2];
    size_t i, code_size, image_size;
    uint8_t *code = image + CODE_OFFSET;

    memset(image, 0, sizeof(image));
    memset(&elf, 0, sizeof(elf));
    memset(ph, 0, sizeof(ph));

    code_size = 0;
    for (i = 0; message[i] != '\0'; i++) {
        code_size = emit_syscall(code, code_size, SYS_putc,
                                 (uint32_t)(uint8_t)message[i]);
    }
    code_size = emit_syscall(code, code_size, SYS_exit, 0);
    image_size = DATA_OFFSET + DATA_SIZE;
    if (CODE_OFFSET + code_size >= DATA_OFFSET || image_size > sizeof(image)) {
        return -E_NO_MEM;
    }

    elf.e_magic = ELF_MAGIC;
    elf.e_elf[0] = 1; /* ELFCLASS32 */
    elf.e_elf[1] = 1; /* little endian */
    elf.e_elf[2] = 1; /* ELF version */
    elf.e_type = 2;    /* executable */
    elf.e_machine = 3; /* i386 */
    elf.e_version = 1;
    elf.e_entry = CODE_VA;
    elf.e_phoff = sizeof(struct elfhdr);
    elf.e_ehsize = sizeof(struct elfhdr);
    elf.e_phentsize = sizeof(struct proghdr);
    elf.e_phnum = 2;

    ph[0].p_type = ELF_PT_LOAD;
    ph[0].p_offset = CODE_OFFSET;
    ph[0].p_va = CODE_VA;
    ph[0].p_filesz = code_size;
    ph[0].p_memsz = code_size;
    ph[0].p_flags = ELF_PF_R | ELF_PF_X;
    ph[0].p_align = UCORE_PAGE_SIZE;

    ph[1].p_type = ELF_PT_LOAD;
    ph[1].p_offset = DATA_OFFSET;
    ph[1].p_va = DATA_VA;
    ph[1].p_filesz = DATA_SIZE;
    ph[1].p_memsz = UCORE_PAGE_SIZE;
    ph[1].p_flags = ELF_PF_R | ELF_PF_W;
    ph[1].p_align = UCORE_PAGE_SIZE;

    memcpy(image, &elf, sizeof(elf));
    memcpy(image + sizeof(elf), ph, sizeof(ph));
    image[DATA_OFFSET] = 0;
    *image_size_store = image_size;
    return 0;
}

int
main(void) {
    size_t image_size;
    int fd, written;
    const char *path = "/native-elf-demo";
    const char *argv[] = { path, NULL };

    if (build_image(&image_size) != 0) {
        cprintf("elfgen: image too large\n");
        return -1;
    }
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) {
        cprintf("elfgen: open failed: %e\n", fd);
        return -1;
    }
    written = write(fd, image, image_size);
    close(fd);
    if (written != (int)image_size) {
        cprintf("elfgen: write failed: %d/%d\n", written, image_size);
        return -1;
    }
    cprintf("elfgen: generated ELF32 (%d bytes), executing\n", image_size);
    return __exec(NULL, argv);
}
