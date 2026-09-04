#ifndef __USER_LIBS_DIR_H__
#define __USER_LIBS_DIR_H__

#include <defs.h>
#include <dirent.h>

typedef struct {
    int fd;
    struct dirent dirent;
} DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
void closedir(DIR *dirp);
int chdir(const char *path);
int mkdir(const char *path);
int link(const char *old_path, const char *new_path);
int unlink(const char *path);
int rename(const char *old_path, const char *new_path);
int getcwd(char *buffer, size_t len);

#endif /* !__USER_LIBS_DIR_H__ */
