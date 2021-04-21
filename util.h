// based on cs3650 starter code

#ifndef UTIL_H
#define UTIL_H

#include <string.h>

static int
streq(const char* aa, const char* bb)
{
    return strcmp(aa, bb) == 0;
}

static int
min(int x, int y)
{
    return (x < y) ? x : y;
}

static int
max(int x, int y)
{
    return (x > y) ? x : y;
}

static int
clamp(int x, int v0, int v1)
{
    return max(v0, min(x, v1));
}

static int
bytes_to_pages(int bytes)
{
    int quo = bytes / 4096;
    int rem = bytes % 4096;
    if (rem == 0) {
        return quo;
    }
    else {
        return quo + 1;
    }
}

static void
join_to_path(char* buf, char* item)
{
    int nn = strlen(buf);
    if (buf[nn - 1] != '/') {
        strcat(buf, "/");
    }
    strcat(buf, item);
}

static void
split_path(const char* path, char* file_name, char* path_to_file)
{
    int last_slash = 0;
    int ii = 0;
    while (path[ii] != 0) {
        if (path[ii] == '/') {
            last_slash = ii;
    	}
        ++ii;
    } 
    strcpy(file_name, &(path[last_slash+1]));
    strncpy(path_to_file, path, last_slash + 1);
    path_to_file[last_slash + 1] = 0;
}

#endif
