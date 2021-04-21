// based on cs3650 starter code

#ifndef INODE_H
#define INODE_H

#include "pages.h"

typedef struct inode {
    int mode; // 1 for dir, 0 for file
    int ptr[6]; // direct pointers
    int iptr;	
    int size;
    time_t times[3]; 
    int ref;
} inode;

typedef struct dir_entry {
    char name[60];
    int node_num;
} dir_entry;

#endif
