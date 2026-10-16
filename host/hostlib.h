/* PORT32X D2b: the C library calls scdpc_host.c uses. Normally the system's; with -DSCDPC_NOLIBC (a 32-bit
   PC without the 32-bit C library) the tiny versions in nolibc32.c. The level code needs 32-bit pointers:
   it keeps pointers in 32-bit slots of the object table (e.g. ENEMY.C ((unsigned char ***)pActwk)[13]). */
#ifndef SCDPC_NOLIBC
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#else
#include <stdarg.h>
typedef unsigned int size_t;
typedef struct nl_file FILE;
#define SEEK_SET 0
#define SEEK_END 2
#define EOF (-1)
FILE *fopen(const char *path, const char *mode);
size_t fread(void *p, size_t s, size_t n, FILE *f);
size_t fwrite(const void *p, size_t s, size_t n, FILE *f);
int fclose(FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int fprintf(FILE *f, const char *fmt, ...);
int printf(const char *fmt, ...);
int snprintf(char *b, size_t n, const char *fmt, ...);
int vsprintf(char *b, const char *fmt, va_list a);
void *malloc(size_t n);
void free(void *p);
void *memset(void *p, int v, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int atoi(const char *s);
int rand(void);
int toupper(int c);
void exit(int code);
extern FILE *stderr;
#endif
