# uCore User File Tools

The VFS and SFS layers already provide the file operations needed by ordinary
user programs:

| Operation | User ABI |
| --- | --- |
| create/open | `open(path, O_CREAT | ...)` |
| overwrite | `open(path, O_WRONLY | O_CREAT | O_TRUNC)` |
| append/write | `write(fd, buffer, length)` |
| read/list | `read`, `opendir`, `readdir` |
| move/rename | `rename(old_path, new_path)` |
| delete | `unlink(path)` |
| directory creation | `mkdir(path)` |
| hard link | `link(old_path, new_path)` |

The bootable SFS image contains the user C sources under `/src`.  The TinyCC
compiler and its freestanding runtime are installed under `/bin/tcc` and
`/tcc`.  After booting the normal shell, a source can be compiled without any
host-side tool:

```text
/bin/tcc /src/hello.c -o /hello2
/hello2
```

The image also contains small user commands:

```text
cat FILE...
cp SOURCE DEST
mkdir DIRECTORY...
mv SOURCE DEST
rm PATH...
touch FILE...
```

`edit FILE` is a bounded line editor suitable for creating and modifying small
text files.  It loads an existing file if present and understands:

```text
p             print the buffer
a TEXT        append one line
d             delete the last line
w             write the file
wq            write and quit
q             quit without saving
h             print command help
```

The editor uses a fixed 64 KiB user buffer.  Larger files should be handled by
`cp`, `cat`, or a future streaming editor rather than loaded into `edit`.

The shell's `cd` is handled in the shell process itself and calls the real
`chdir` syscall, so subsequent commands use the changed working directory.
