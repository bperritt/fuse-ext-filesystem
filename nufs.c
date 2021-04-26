// based on cs3650 starter code

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <bsd/string.h>
#include <assert.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "pages.h"
#include "slist.h"
#include "bitmap.h"
#include "inode.h"
#include "util.h"

static void* fs_base = 0;
static inode* root = 0;

#define DIRECTORY_MAX 64 // max number of files that can be contained within a directory
#define DIRECT_POINTERS 6 // number of direct pointers in an inode
#define PAGE_SIZE 4096 // page size
#define MAX_FILES 251 // max number of files the fs can hold

// function that uses a file path to access the inodes and then returns the inum
int
find_file(const char *path)
{
    if (streq("/", path)) { //is it the root?
        return 0;
    }

    slist* path_names = s_split(path, '/'); //get the tokenized list of names
    path_names = path_names->next; //get the first node after the root
    inode* curr = root;
    int rv = 0;
    while (path_names != 0) { //make sure theres still another node to access
        void* files = pages_get_page(curr->ptr[0]); //access the page associated with the inode
        for (int ii = 0; ii < DIRECTORY_MAX; ++ii) { //go through the directory entries
            dir_entry* dir = (dir_entry*) files + ii; 
            if (dir->node_num != 0 && streq(dir->name, path_names->data)) { //check if the dir matches the path name
                curr = root + dir->node_num; 
                rv = dir->node_num; //set the return number to be the inum
                break;
            }
            else if (ii == DIRECTORY_MAX - 1) { //couldnt find the given name
                return -1;
            }    
        }
        path_names = path_names->next; //iterate through
    }
    s_free(path_names);
    return rv;
}

// implementation for: man 2 access
// Checks if a file exists.
int
nufs_access(const char *path, int mask)
{
    int rv = (find_file(path) >= 0) ? 0 : -1;
    printf("access(%s, %04o) -> %d\n", path, mask, rv);
    return rv;
}

// implementation for: man 2 stat
// gets an object's attributes (type, permissions, size, etc)
int
nufs_getattr(const char *path, struct stat *st)
{
    int rv = find_file(path); //get the inum

    if (rv != -1) { //set all of the stats for the inode
        inode* node = root + rv; //get the inode by offsetting
        st->st_mode = node->mode;
        st->st_size = node->size;
        st->st_ino = rv;
        st->st_nlink = node->ref;
        struct timespec t = {node->times[0], 0};
        st->st_atim = t;
        t.tv_sec = node->times[1];
        st->st_mtim = t;
        t.tv_sec = node->times[2];
        st->st_ctim = t;
        printf("getattr(%s) -> (%d) {mode: %04o, size: %ld}\n", path, 0, st->st_mode, st->st_size);
        return 0;
    }
    printf("getattr(%s) -> (%d) {mode: %04o, size: %ld}\n", path, -ENOENT, st->st_mode, st->st_size);
    return -ENOENT; //if the inode wasnt found
}

// implementation for: man 2 readdir
// lists the contents of a directory
int
nufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
             off_t offset, struct fuse_file_info *fi)
{ 
    int rv;
    struct stat st;

    inode* node = root + find_file(path); //get the inode we want
    time(&node->times[0]);
    void* files = pages_get_page(node->ptr[0]); //get the page that the inode gives us
    rv = nufs_getattr(path, &st); 
    assert(rv == 0);
    filler(buf, ".", &st, 0); //add this to the printing

    if (!streq(path, "/")) {
        char file_name[60]; 
        char path_to_file[60];
        split_path(path, file_name, path_to_file); //get the tokenized names
        rv = nufs_getattr(path_to_file, &st); 
        assert(rv == 0);
        filler(buf, "..", &st, 0);
    }

    for (int ii = 0; ii < DIRECTORY_MAX; ++ii) { //list the directory info
        dir_entry* dir = (dir_entry*) files + ii;
        if (dir->node_num != 0) {
            rv = nufs_getattr(path, &st);
            assert(rv == 0);   
            filler(buf, dir->name, &st, 0);
        }
    }    

    printf("readdir(%s) -> %d\n", path, rv);
    return 0;
}

int
alloc_inode(mode_t mode)
{
    void* ibm = get_inode_bitmap(); //get the inode bitmap
    for (int ii = 0; ii < MAX_FILES; ++ii) {  //iterate through it
        if (!bitmap_get(ibm, ii)) { //if theres an empty slot, allocate there
            inode* node = root + ii;
            node->size = S_ISDIR(mode) ? PAGE_SIZE : 0;
            node->mode = mode;
            node->ptr[0] = alloc_page();
            for (int ii = 1; ii < DIRECT_POINTERS; ++ii) {
                node->ptr[ii] = -1;
            }
            node->ref = 1;
            node->iptr = -1;
            bitmap_put(ibm, ii, 1); //change it to 1
	    printf("+ alloc_inode() -> %d\n", ii);
            return ii;
        }
    }
    return -1; //there werent any empty slots 
}  

// mknod makes a filesystem object like a file or directory
// called for: man 2 open, man 2 link
int
nufs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int rv = 0;
    char file_name[60]; 
    char path_to_file[60];
    split_path(path, file_name, path_to_file); //get the tokenized names

    inode* node = find_file(path_to_file) + root; //get the inode at the path name
    void* files = pages_get_page(node->ptr[0]);    
    int inode_num = alloc_inode(mode); //get the inode number and allocate it
    assert(inode_num != -1);

    inode* new_node = inode_num + root;
    time_t t;
    time(&t);
    new_node->times[0] = t;
    new_node->times[1] = t;
    new_node->times[2] = t;

    for (int ii = 0; ii < DIRECTORY_MAX; ++ii) { //go through the directories
        dir_entry* dir = (dir_entry*) files + ii;    
        if (dir->node_num == 0) {
            strcpy(dir->name, file_name);
            dir->node_num = inode_num;
            time_t t;
            time(&t);
            node->times[1] = t;
            node->times[2] = t;
            break; //we reached the end so break out
        }
        else if (ii == DIRECTORY_MAX - 1) { //couldnt find it
            rv = -1;
        }
    }    
    printf("mknod(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

// most of the following callbacks implement
// another system call; see section 2 of the manual
int
nufs_mkdir(const char *path, mode_t mode)
{
    int rv = nufs_mknod(path, mode | 040000, 0); //make a new directory with the needed mode
    printf("mkdir(%s) -> %d\n", path, rv);
    return rv;
}

int
nufs_unlink(const char *path)
{
    int inode_num = find_file(path);
    inode* node = inode_num + root; //get to the wanted inode
    node->ref--; //decrement because we are unlinking
    time(&node->times[2]);
   
    char file_name[60];
    char path_to_file[60];
    split_path(path, file_name, path_to_file);
    
    inode* prev_node = find_file(path_to_file) + root;
    void* files = pages_get_page(prev_node->ptr[0]);

    for (int ii = 0; ii < DIRECTORY_MAX; ++ii) { //go through the directories
        dir_entry* dir = (dir_entry*) files + ii;
        if (dir->node_num != 0 && streq(dir->name, file_name)) {
            dir->node_num = 0;
            time_t t;
            time(&t);
            prev_node->times[1] = t;
            prev_node->times[2] = t;
            break;
        }
        else if (ii == DIRECTORY_MAX - 1) { //reached the end
            printf("unlink(%s) -> %d\n", path, -1); 
            return -1;
        }
    }

    if (node->ref <= 0) { //ready to actually get deleted?
	int pages = node->size / PAGE_SIZE;
        int pages_to_free = node->size % PAGE_SIZE == 0 ? pages : pages + 1; //find how many pages we need to free

        for (int ii = 0; ii < min(pages_to_free, DIRECT_POINTERS); ++ii) {
            free_page(node->ptr[ii]); //free all the pages we need
        } 

        if (node->iptr != -1) { //are there any iptrs?
            int* pointers = (int*) pages_get_page(node->iptr);
            for (int ii = 0; ii < pages_to_free - DIRECT_POINTERS; ++ii) {
                free_page(*(pointers + ii)); 
            } 
            free_page(node->iptr);
        }    
      
        void* ibm = get_inode_bitmap();
        bitmap_put(ibm, inode_num, 0); //free on the bitmap
	printf("+ free_inode() -> %d\n", inode_num);
    }
    printf("unlink(%s) -> %d\n", path, 0);
    return 0;
}

int
nufs_link(const char *from, const char *to)
{
    int rv = 0;
    int from_node_index = find_file(from); //get the inode number

    char file_name[60];
    char path_to_file[60];
    split_path(to, file_name, path_to_file);

    inode* to_node = find_file(path_to_file) + root; //get the needed inode
    inode* from_node = root + from_node_index;
    from_node->ref++; //increment the refs because we are adding another link
    time(&from_node->times[2]);
    
    void* files = pages_get_page(to_node->ptr[0]);
    for (int ii = 0; ii < DIRECTORY_MAX; ++ii) {
        dir_entry* dir = (dir_entry*) files + ii;
        if (dir->node_num == 0) { 
            strcpy(dir->name, file_name);
            dir->node_num = from_node_index;
            time_t t;
            time(&t);
            to_node->times[1] = t;
            to_node->times[2] = t;
            break;
        }
        else if (ii == DIRECTORY_MAX - 1) {
            rv = -1;
        }
    }
    printf("link(%s => %s) -> %d\n", from, to, rv);
    return rv;
}

int
nufs_rmdir(const char *path)
{
    int rv = 0; 
    rv = nufs_unlink(path); 
    printf("rmdir(%s) -> %d\n", path, rv);
    return rv;
}

// implements: man 2 rename
// called to move a file within the same filesystem
int
nufs_rename(const char *from, const char *to)
{
    int old_node_num = find_file(from); //get the old name inum
    int rv = 0;

    char file_name[60];
    char path_to_file[60];
    split_path(to, file_name, path_to_file);

    // link to new path
    inode* new_path = root + find_file(path_to_file); 
    void* files = pages_get_page(new_path->ptr[0]); 
    for (int ii = 0; ii < DIRECTORY_MAX; ++ii) {
        dir_entry* dir = (dir_entry*) files + ii;
        if (dir->node_num == 0) {
            strcpy(dir->name, file_name); //copy the new name to the directory
            dir->node_num = old_node_num;
            time_t t;
            time(&t);
            new_path->times[1] = t;
            new_path->times[2] = t; 
            break;
        }
    }

    // increase ref so node isn't deleted
    inode* node = root + old_node_num; 
    node->ref++;
    time(&node->times[2]);

    // unlink old path
    nufs_unlink(from);

    printf("rename(%s => %s) -> %d\n", from, to, rv);
    return rv;
}

int
nufs_chmod(const char *path, mode_t mode)
{
    int rv = find_file(path);
    if (rv >= 0) {
        inode* node = root + rv; //get the node we are changing
        time(&node->times[2]);
        node->mode = mode; //change the mode

        char file_name[60];
        char path_to_file[60];
        split_path(path, file_name, path_to_file);
        inode* path_node = root + rv; //get the path inode to change timestamps
        time_t t;
        time(&t);
        path_node->times[1] = t;
        path_node->times[2] = t;
        rv = 0;
    } else {
        rv = -1;
    }  
    printf("chmod(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

int
nufs_truncate(const char *path, off_t size)
{
    int rv = 0;
    printf("truncate(%s, %ld bytes) -> %d\n", path, size, rv);
    return rv;
}

// this is called on open, but doesn't need to do much
// since FUSE doesn't assume you maintain state for
// open files.
int
nufs_open(const char *path, struct fuse_file_info *fi)
{
    int rv = 0;
    printf("open(%s) -> %d\n", path, rv);
    return rv;
}

// Actually read data
int
nufs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int rv = size;
    int index = find_file(path); 
    inode* node = root + index; // get the inode for the file
    time(&node->times[0]);

    int total = size + offset; // the bytes we need
    int start_page = offset / PAGE_SIZE; 
    int end_page = total / PAGE_SIZE;

    int start_offset = offset % PAGE_SIZE; //so when we start at the first page, it starts at the right location
    int end_offset = total % PAGE_SIZE; // same as the first page (need an offset)
    int buffer_offset = 0;

    // read the data
    for (int ii = start_page; ii <= end_page; ++ii) {

        void* page;
        if (ii < DIRECT_POINTERS) {
            page = pages_get_page(node->ptr[ii]);
        } else {
            int* pointers = (int*) pages_get_page(node->iptr); 
            int index = ii - DIRECT_POINTERS;
            page = pages_get_page(*(pointers + index));
        }

        int write_size = PAGE_SIZE;
        if (ii == start_page) { //if its the first page, find the offset
            write_size -= start_offset;
            page = (char*) page + start_offset; 
        }
        else if (ii == end_page) { //if the end, add the offset
            write_size = end_offset;
        }
        memcpy(buf + buffer_offset, page, write_size); //copy the file info over to the buffer to read it
        buffer_offset += write_size;
    }
    printf("read(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
    return rv;
}

// Actually write data
int
nufs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int rv = size;
    int total = size + offset;
    int start_page = offset / PAGE_SIZE; //find the starting point
    int end_page = total / PAGE_SIZE; //find the end point
    int index = find_file(path);
	inode* node = root + index;
    time_t t;
    time(&t);
    node->times[1] = t;
    node->times[2] = t;

    // allocate the necessary pages to write to
    for (int ii = 1; ii <= end_page; ++ii) {
        if (ii >= DIRECT_POINTERS) { //not enough room in the regular pointers - go to the iptr
            if (node->iptr == -1) { 
                node->iptr = alloc_page();
                assert(node->iptr != -1);
            }
            int* pointers = (int*) pages_get_page(node->iptr); 
            int index = ii - DIRECT_POINTERS;
            if (*(pointers + index) == 0) {
                *(pointers + index) = alloc_page(); //allocate a new page 
                assert(*(pointers + index) != -1);
            }
        } else {
            if (node->ptr[ii] == -1) {
                node->ptr[ii] = alloc_page();
                assert(node->ptr[ii] != -1);
            }
        }
    }
    
    // actually write to the pages
    node->size = max(node->size, total); 
    int start_offset = offset % PAGE_SIZE;
    int end_offset = total % PAGE_SIZE;
    for (int ii = start_page; ii <= end_page; ++ii) {
        void* page;
        if (ii < DIRECT_POINTERS) { //need to go to iptrs
            page = pages_get_page(node->ptr[ii]);
        } else {
            int* pointers = (int*) pages_get_page(node->iptr);
            int index = ii - DIRECT_POINTERS;
            page = pages_get_page(*(pointers + index)); //get the page 
        }
        int write_size = PAGE_SIZE;
        if (ii == start_page) { //if at first page, offset it
            write_size -= start_offset; 
            page = (char*) page + start_offset;
        }
        else if (ii == end_page) { //if at last page, offset it
            write_size = end_offset;
        }
        memcpy(page, buf, write_size); //copy it over
    }
 	
    printf("write(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
    return rv;
}

int
nufs_symlink(const char *from, const char *to)
{
    int rv = nufs_mknod(to, S_IFLNK | 0777, 0);
    nufs_write(to, from, strlen(from), 0, 0);
    printf("symlink(%s => %s) -> %d\n", from, to, rv);
    return rv;
}

int
nufs_readlink(const char* path, char* buf, size_t size) {
    int rv = 0;
    nufs_read(path, buf, size, 0, 0);
    printf("readlink(%s => %s) -> %d\n", path, buf, rv);
    return rv;  
}

// Update the timestamps on a file or directory.
int
nufs_utimens(const char* path, const struct timespec ts[2])
{
    int rv = find_file(path);
    if (rv >= 0) {
	    inode* node = root + rv;
	    node->times[0] = ts[0].tv_sec;
	    node->times[1] = ts[1].tv_sec;
	    rv = 0;
    } else {
        rv = -1;
    }	
    printf("utimens(%s, [%ld, %ld; %ld %ld]) -> %d\n",
           path, ts[0].tv_sec, ts[0].tv_nsec, ts[1].tv_sec, ts[1].tv_nsec, rv);
	return rv;
}

// Extended operations
int
nufs_ioctl(const char* path, int cmd, void* arg, struct fuse_file_info* fi,
           unsigned int flags, void* data)
{
    int rv = 0;
    printf("ioctl(%s, %d, ...) -> %d\n", path, cmd, rv);
    return rv;
}

void
nufs_init_ops(struct fuse_operations* ops)
{
    memset(ops, 0, sizeof(struct fuse_operations));
    ops->access   = nufs_access;
    ops->getattr  = nufs_getattr;
    ops->readdir  = nufs_readdir;
    ops->mknod    = nufs_mknod;
    ops->mkdir    = nufs_mkdir;
    ops->link     = nufs_link;
    ops->symlink  = nufs_symlink;
    ops->readlink = nufs_readlink;
    ops->unlink   = nufs_unlink;
    ops->rmdir    = nufs_rmdir;
    ops->rename   = nufs_rename;
    ops->   chmod    = nufs_chmod;
    ops->truncate = nufs_truncate;
    ops->open	  = nufs_open;
    ops->read     = nufs_read;
    ops->write    = nufs_write;
    ops->utimens  = nufs_utimens;
    ops->ioctl    = nufs_ioctl;
};

struct fuse_operations nufs_ops;
    
int
main(int argc, char *argv[])
{
    assert(argc > 2 && argc < 6);
    pages_init(argv[--argc]);
    nufs_init_ops(&nufs_ops);
    root = pages_get_page(1);
    if (root->size == 0) { //allocate the pages
        alloc_page();
        alloc_page();
        alloc_page();
        alloc_page();
        alloc_inode(040755); 
    }   
    return fuse_main(argc, argv, &nufs_ops, NULL);
}
;
