// Whole files at a time.
#ifndef FILES_H
#define FILES_H

#include <stdbool.h>

bool file_exists(const char *path);
// The whole file, NUL-terminated and malloc'd, or NULL (also when it is over max). *len gets its size.
char *file_read(const char *path, int max, int *len);
bool file_write(const char *path, const char *buf, int len);
// Written, then read back and compared.
bool file_write_checked(const char *path, const char *buf, int len);

#endif
