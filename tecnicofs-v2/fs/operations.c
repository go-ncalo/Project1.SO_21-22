#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

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
            pthread_rwlock_rdlock(&inode->rwlock);
            if (inode->i_size > 0) {
                for (int i = 0; i < DIRECT_BLOCKS; i++) {
                    // Frees the inode's direct blocks that are initialized
                    if (inode->direct_blocks[i] != -1) {
                        if (data_block_free(inode->direct_blocks[i]) == -1) {
                            pthread_rwlock_unlock(&inode->rwlock);
                            return -1;
                        }
                    }
                }
                // Frees the inode's indirect block if initialized
                if (inode->indirect_block!=-1) {
                    int* indirect_block = data_block_get(inode->indirect_block); 
                    for (int i = 0; i < INDIRECT_BLOCKS; i++) {
                        if (indirect_block[i] != -1) {
                            if (data_block_free(indirect_block[i]) == -1) {
                                pthread_rwlock_unlock(&inode->rwlock);
                                return -1;
                            }
                        }
                    }
                    if (data_block_free(inode->indirect_block) == -1) {
                        pthread_rwlock_unlock(&inode->rwlock);
                        return -1;
                    }
                }
                inode->i_size = 0;
            }
            pthread_rwlock_unlock(&inode->rwlock);
        }    

        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            pthread_rwlock_rdlock(&inode->rwlock);
            offset = inode->i_size;
            pthread_rwlock_unlock(&inode->rwlock);
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
        // Lock of the root
        pthread_rwlock_wrlock(&inode_get(ROOT_DIR_INUM)->rwlock);

        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            pthread_rwlock_unlock(&inode_get(ROOT_DIR_INUM)->rwlock);
            inode_delete(inum);
            return -1;
        }
        pthread_rwlock_unlock(&inode_get(ROOT_DIR_INUM)->rwlock);

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

int tfs_close(int fhandle) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    inode_t *inode = inode_get(file->of_inumber);
    pthread_rwlock_wrlock(&inode->rwlock);
    int return_value = remove_from_open_file_table(fhandle); 
    pthread_rwlock_unlock(&inode->rwlock);
    return return_value;
    }

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    size_t bytes_to_write, bytes_written = 0;
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    pthread_mutex_lock(&file->mutex);
    inode_t *inode = inode_get(file->of_inumber);
    pthread_mutex_unlock(&file->mutex);

    if (inode == NULL) {
        return -1;
    }


    if (to_write > 0) {
        // Number of blocks needed to write the size given in the input
        int blocks_to_write = (int) to_write / BLOCK_SIZE;

        pthread_mutex_lock(&file->mutex);
        // Block where the current offset of the file is
        int offset_block = (int) file->of_offset / BLOCK_SIZE;

        // In case the numbers are not divisible we need to increase
        // the variables by 1
        if ((int)to_write % BLOCK_SIZE != 0) {
            blocks_to_write++;
        }

        if (file->of_offset > BLOCK_SIZE) {
            if (file->of_offset % BLOCK_SIZE != 0) {
                offset_block++;
            }
        }

        pthread_rwlock_wrlock(&inode->rwlock);

        // In case the size of the write doesn't fit in the direct blocks
        // and the index block hasn't been allocated
        if (offset_block + blocks_to_write >= DIRECT_BLOCKS && inode->indirect_block == -1) {
            inode->indirect_block = data_block_alloc();
            int *indirect_block = data_block_get(inode->indirect_block);
            for (size_t i = 0; i < INDIRECT_BLOCKS; i++) {
                indirect_block[i] = -1;
            }
        }

        int *indirect_block = NULL;

        if (offset_block + blocks_to_write >= DIRECT_BLOCKS) 
            indirect_block = data_block_get(inode->indirect_block); 

        // iterates from the block where the current offset is to the one
        // needed to write everything
        for (int i = offset_block; i <= offset_block + blocks_to_write; i++) {
            void *block;
            // where the offset is from the beggining of its block
            size_t offset = file->of_offset % BLOCK_SIZE;
            
            // DIRECT BLOCKS
            if (i < DIRECT_BLOCKS) {
                if (inode->direct_blocks[i] == -1) {
                    inode->direct_blocks[i] = data_block_alloc();
                }
                block = data_block_get(inode->direct_blocks[i]);
                if (block == NULL) {
                    pthread_rwlock_unlock(&inode->rwlock);
                    pthread_mutex_unlock(&file->mutex);
                    return -1;
                }
            // INDIRECT BLOCKS
            } else {
                if (indirect_block[i] == -1) {
                    indirect_block[i] = data_block_alloc();
                } 
                block = data_block_get(indirect_block[i]);
                if (block == NULL) {
                    pthread_rwlock_unlock(&inode->rwlock);
                    pthread_mutex_unlock(&file->mutex);
                    return -1;
                }
            }
            // Calculates the number of bytes that will be
            // written in the current block
            if (offset != 0) {
                if (to_write > BLOCK_SIZE - offset) {
                    bytes_to_write = BLOCK_SIZE - offset;
                } else {
                    bytes_to_write = to_write;
                }
            } else if (to_write > BLOCK_SIZE) {
                bytes_to_write = BLOCK_SIZE;
            } else {
                bytes_to_write = to_write;
            }

            // Perform the actual write and updates the offset
            // of the file accordingly
            memcpy(block + offset, buffer, bytes_to_write);
            file->of_offset += bytes_to_write;
            to_write -= bytes_to_write;
            bytes_written += bytes_to_write;
        }

        if (file->of_offset > inode->i_size) {
            inode->i_size = file->of_offset;
        }

        pthread_rwlock_unlock(&inode->rwlock);
        pthread_mutex_unlock(&file->mutex);
    } 
    return (ssize_t)bytes_written;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    size_t bytes_to_read, bytes_read = 0;
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

     /*From the open file table entry, we get the inode */
    pthread_mutex_lock(&file->mutex);
    inode_t *inode = inode_get(file->of_inumber);

    if (inode == NULL) {
        pthread_mutex_unlock(&file->mutex);
        return -1;
    }


     /* Determine how many bytes to read */
    pthread_rwlock_rdlock(&inode->rwlock);
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }
    pthread_rwlock_unlock(&inode->rwlock);
    pthread_mutex_unlock(&file->mutex);

    if (to_read > 0) {
        // Number of blocks needed to read
        int block_to_read = (int) to_read / BLOCK_SIZE;

        pthread_mutex_lock(&file->mutex);
        // Block where the current offset of the file is
        int offset_block = (int) file->of_offset / BLOCK_SIZE;

        // In case the numbers are not divisible we need to increase
        // the variables by 1
        if (file->of_offset > BLOCK_SIZE) {
            if (file->of_offset % BLOCK_SIZE != 0) {
                offset_block++;
            }
        }

        if ((int)to_read % BLOCK_SIZE != 0) {
            block_to_read++;
        }

        int *indirect_block=NULL;

        pthread_rwlock_rdlock(&inode->rwlock);
        if (offset_block + block_to_read>=DIRECT_BLOCKS) 
            indirect_block = data_block_get(inode->indirect_block); 

        // iterates from the block where the current offset is to the one
        // needed to read everything
        for (int i = offset_block; i <= offset_block + block_to_read; i++) {
            void *block;
            // where the offset is from the beggining of its block
            size_t offset = file->of_offset % BLOCK_SIZE;

            // DIRECT BLOCKS
            if (i < DIRECT_BLOCKS) {
                block = data_block_get(inode->direct_blocks[i]);
                if (block == NULL) {
                    pthread_mutex_unlock(&file->mutex);
                    pthread_rwlock_unlock(&inode->rwlock);
                    return -1;
                }
            // INDIRECT BLOCKS
            } else {
                block = data_block_get(indirect_block[i]);
                if (block == NULL) {
                    pthread_mutex_unlock(&file->mutex);
                    pthread_rwlock_unlock(&inode->rwlock);
                    return -1;
                }
            }
            // Calculates the number of bytes that will be
            // read in the current block
            if (offset != 0) {
                if (to_read > BLOCK_SIZE - offset) {
                    bytes_to_read = BLOCK_SIZE - offset;
                } else {
                    bytes_to_read = to_read;
                }
            } else if (to_read > BLOCK_SIZE) {
                bytes_to_read = BLOCK_SIZE;
            } else {
                bytes_to_read = to_read;
            }

            // Perform the actual read and updates the offset
            // of the file accordingly
            memcpy(buffer, block + offset, bytes_to_read);
            file->of_offset += bytes_to_read;
            to_read -= bytes_to_read;
            bytes_read += bytes_to_read;
        }

        pthread_mutex_unlock(&file->mutex);
        pthread_rwlock_unlock(&inode->rwlock);
    }

    return (ssize_t)bytes_read;
} 


int tfs_copy_to_external_fs(char const *source_path, char const *dest_path) {
    FILE *dest_pt;
    int source_inumber = tfs_lookup(source_path);
    dest_pt = fopen(dest_path,"w");
    if (source_inumber == -1){
        fclose(dest_pt);
        return -1;
    }

    if (dest_pt == NULL) {
        return -1;
    }
    // Max number of bytes that can be read
    char *buffer = malloc(BLOCK_SIZE*DATA_BLOCKS);
    int fhandle_source = tfs_open(source_path,0);

    ssize_t n_bytes = tfs_read(fhandle_source, buffer, BLOCK_SIZE*DATA_BLOCKS);
    if (n_bytes == -1) {
        free(buffer);
        return -1;
    }

    // Performs the copy to the external file
    fwrite(buffer,1,(size_t)n_bytes,dest_pt);

    free(buffer);
    tfs_close(fhandle_source);
    fclose(dest_pt);
    return 0;
}