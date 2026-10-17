// GraalVM's static libraries for Linux are built against glibc: the few glibc-only symbols they reference, on bionic
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// A native image linked here is never a static executable
int __svm_vm_is_static_binary = 0;

int *__errno_location(void) { return __errno(); }

int __xstat(int ver, const char *path, struct stat *buf) { (void)ver; return stat(path, buf); }
int __xstat64(int ver, const char *path, struct stat *buf) { (void)ver; return stat(path, buf); }
int __lxstat64(int ver, const char *path, struct stat *buf) { (void)ver; return lstat(path, buf); }
int __fxstat64(int ver, int fd, struct stat *buf) { (void)ver; return fstat(fd, buf); }
int __xmknod(int ver, const char *path, mode_t mode, dev_t *dev) { (void)ver; return mknod(path, mode, *dev); }

// The XSI strerror_r: bionic's own strerror_r symbol (_GNU_SOURCE renames the declaration to __gnu_strerror_r)
extern int xsi_strerror_r(int, char *, size_t) __asm__("strerror_r");
int __xpg_strerror_r(int errnum, char *buf, size_t len) { return xsi_strerror_r(errnum, buf, len); }

char *__strtok_r(char *s, const char *delim, char **save) { return strtok_r(s, delim, save); }
char *__strdup(const char *s) { return strdup(s); }
ssize_t __getdelim(char **line, size_t *n, int delim, FILE *stream) { return getdelim(line, n, delim, stream); }

int __isoc99_sscanf(const char *s, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsscanf(s, format, args);
    va_end(args);
    return result;
}

// glibc's character class table (indexed from -128), with its bit layout (little-endian)
#define GLIBC_IS(bit) ((bit) < 8 ? ((1 << (bit)) << 8) : ((1 << (bit)) >> 8))
static unsigned short ctype_table[384];
static const unsigned short *ctype_b = ctype_table + 128;

__attribute__((constructor)) static void init_ctype(void) {
    for (int c = -128; c < 256; c++) {
        int ch = c < 0 ? c + 256 : c;
        unsigned short v = 0;
        if (ch < 128) {
            if (isupper(ch)) v |= GLIBC_IS(0);
            if (islower(ch)) v |= GLIBC_IS(1);
            if (isalpha(ch)) v |= GLIBC_IS(2);
            if (isdigit(ch)) v |= GLIBC_IS(3);
            if (isxdigit(ch)) v |= GLIBC_IS(4);
            if (isspace(ch)) v |= GLIBC_IS(5);
            if (isprint(ch)) v |= GLIBC_IS(6);
            if (isgraph(ch)) v |= GLIBC_IS(7);
            if (isblank(ch)) v |= GLIBC_IS(8);
            if (iscntrl(ch)) v |= GLIBC_IS(9);
            if (ispunct(ch)) v |= GLIBC_IS(10);
            if (isalnum(ch)) v |= GLIBC_IS(11);
        }
        ctype_table[c + 128] = v;
    }
}

const unsigned short **__ctype_b_loc(void) { return &ctype_b; }
