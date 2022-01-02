#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tfs_init() {
    state_init();

    /* create root inode */
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    state_destroy();
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}


int tfs_lookup(char const *name) {
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(ROOT_DIR_INUM, name);
}

int tfs_open(char const *name, int flags) {
    int inum;
    size_t offset;

    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }

    inum = tfs_lookup(name);
    if (inum >= 0) {
        /* The file already exists */
        inode_t *inode = inode_get(inum);
        if (inode == NULL) {
            return -1;
        }

        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                for (int i = 0; i < DIRECT_BLOCKS; i++) {
                    if (inode->direct_blocks[i] != -1) {
                        if (data_block_free(inode->direct_blocks[i]) == -1) {
                            return -1;
                        }
                    }
                }
                inode->i_size = 0;
            }
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
    } else if (flags & TFS_O_CREAT) {
        /* The file doesn't exist; the flags specify that it should be created*/
        /* Create inode */
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1;
        }
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
            return -1;
        }
        offset = 0;
    } else {
        return -1;
    }

    /* Finally, add entry to the open file table and
     * return the corresponding handle */
    return add_to_open_file_table(inum, offset);

    /* Note: for simplification, if file was created with TFS_O_CREAT and there
     * is an error adding an entry to the open file table, the file is not
     * opened but it remains created */
}


int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    /* Determine how many bytes to write */
    //if (to_write + file->of_offset > BLOCK_SIZE) {
        //to_write = BLOCK_SIZE - file->of_offset;
    //}

    size_t total_bytes = to_write + file->of_offset;

    int blocks_to_write = (int)to_write / BLOCK_SIZE;

    if ((int)to_write % BLOCK_SIZE != 0) {
        blocks_to_write++;
    }

    int first_direct_index_free=-1;
    for (int i=0; i<DIRECT_BLOCKS; i++) {
        if (inode->direct_blocks[i]==-1) {
            first_direct_index_free=i;
            break;
        }
    }

    int *indirect_block = data_block_get(inode->indirect_block);
    int first_indirect_index_free=-1;
    for (int i=0; i<INDIRECT_BLOCKS; i++) {
        if (indirect_block[i]==-1) {
            first_indirect_index_free=i;
            break;
        }
    }
     if (to_write > 0) {

        size_t bytes_to_write=to_write;
        size_t to_write_block=BLOCK_SIZE-(inode->i_size%BLOCK_SIZE);
        int block_index=-1;

         if (to_write_block) { //if a block is not full
            if (first_direct_index_free!=-1 && first_direct_index_free<DIRECT_BLOCKS) {
                block_index=inode->direct_blocks[first_direct_index_free-1];
            }
            else if (first_indirect_index_free!=-1 && first_direct_index_free==indirect_block[0]) {
                block_index=inode->direct_blocks[DIRECT_BLOCKS-1];
            }
            else if (first_indirect_index_free!=-1 && first_indirect_index_free<INDIRECT_BLOCKS) {
                block_index=indirect_block[first_indirect_index_free-1];
            }
            memcpy(data_block_get(block_index), buffer, to_write_block);
            buffer+=to_write_block;
            bytes_to_write-=to_write_block;
         }

        while (inode->i_size < total_bytes) {
            if (first_direct_index_free!=-1 && first_direct_index_free<DIRECT_BLOCKS) {
                inode->direct_blocks[first_direct_index_free] = data_block_alloc();
                block_index = inode->direct_blocks[first_direct_index_free];
                first_direct_index_free++;
            }
            else if (first_indirect_index_free!=-1 && first_indirect_index_free<INDIRECT_BLOCKS){
                indirect_block[first_indirect_index_free] = data_block_alloc();
                block_index = indirect_block[first_indirect_index_free];
                first_indirect_index_free++;
            }
            else {
                return -1;
            }

            if (bytes_to_write>BLOCK_SIZE) 
                to_write_block=BLOCK_SIZE;
            else
                to_write_block=bytes_to_write;


            memcpy(data_block_get(block_index), buffer, to_write_block);
            buffer+=to_write_block;
            inode->i_size+=BLOCK_SIZE;
            bytes_to_write-=to_write_block;
        }

        //void *block = data_block_get(inode->i_data_block);
        //if (block == NULL) {
            //return -1;
        //}

        /* Perform the actual write */
        //memcpy(block + file->of_offset, buffer, to_write);

        /* The offset associated with the file handle is
         * incremented accordingly */
        file->of_offset += to_write;
        if (file->of_offset > inode->i_size) {
            inode->i_size = file->of_offset;
        }
    } 

    return (ssize_t)to_write;
}


/*ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

     From the open file table entry, we get the inode 
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

     Determine how many bytes to read 
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    if (to_read > 0) {
        void *block = data_block_get(inode->i_data_block);
        if (block == NULL) {
            return -1;
        }

         Perform the actual read 
        memcpy(buffer, block + file->of_offset, to_read);
         The offset associated with the file handle is
         * incremented accordingly 
        file->of_offset += to_read;
    }

    return (ssize_t)to_read;
} 

int tfs_copy_to_external_fs(char const *source_path, char const *dest_path) {
    FILE *dest_pt;
    int source_inumber=tfs_lookup(source_path);
    dest_pt=fopen(dest_path,"w");
    if (source_inumber==-1 || dest_pt==NULL) {
        return -1;
    }
    char *buffer = malloc(BLOCK_SIZE*DATA_BLOCKS);
    int fhandle_source=tfs_open(source_path,0);
    ssize_t n_bytes=tfs_read(fhandle_source, buffer, BLOCK_SIZE*DATA_BLOCKS);
    if (n_bytes == -1) {
        return -1;
    }
    fwrite(buffer,1,(size_t)n_bytes,dest_pt);

    free(buffer);
    tfs_close(fhandle_source);
    fclose(dest_pt);
    return 0;
} */
