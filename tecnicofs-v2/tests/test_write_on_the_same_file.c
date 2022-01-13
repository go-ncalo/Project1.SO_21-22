#include "../fs/operations.h"
#include <assert.h>
#include <string.h>

#define COUNT 80
#define DOUBLECOUNT 160
#define SIZE 256

/**
   This tests writes on the same file 20 blocks of
   data, twice. And then it has 4 threads that reads
   everything that was written.
 */

void* read (void* args);
void* write (void* args);
void* write_same_file( void* args);

typedef struct {
    char *path;
    char input[SIZE];
} args_struct;

int main() {

    args_struct args;
    args.path = "/f1";
    memset(args.input, 'A', SIZE);

    assert(tfs_init() != -1);
    pthread_t write1, write2, read1, read2, read3, read4;


    assert(pthread_create(&write1,NULL, &write, &args) == 0);
    assert(pthread_join(write1, NULL) == 0);
    assert(pthread_create(&write2,NULL, &write_same_file, &args) == 0);
    assert(pthread_join(write2, NULL) == 0);
    assert(pthread_create(&read1,NULL, &read, &args) == 0);
    assert(pthread_create(&read2,NULL, &read, &args) == 0);
    assert(pthread_create(&read3,NULL, &read, &args) == 0);
    assert(pthread_create(&read4,NULL, &read, &args) == 0);
    assert(pthread_join(read1, NULL) == 0);
    assert(pthread_join(read2, NULL) == 0);
    assert(pthread_join(read3, NULL) == 0);
    assert(pthread_join(read4, NULL) == 0);
    

    printf("Successful test\n");

    return 0;
}

void* read (void* args) {
    char output [SIZE];

    int fd = tfs_open(((args_struct*)args)->path, 0);
    assert(fd != -1 );

    for (int i = 0; i < DOUBLECOUNT; i++) {
        assert(tfs_read(fd, output, SIZE) == SIZE);
        assert(memcmp(((args_struct*)args)->input, output, SIZE) == 0);
    }

    assert(tfs_close(fd) != -1);
    return 0;
}

void* write (void* args) {
    int fd = tfs_open(((args_struct*)args)->path, TFS_O_CREAT);
    assert(fd != -1);
    for (int i = 0; i < COUNT; i++) {
        assert(tfs_write(fd, ((args_struct*)args)->input, SIZE) == SIZE);
    }
    assert(tfs_close(fd) != -1);
    return 0;
}
void* write_same_file (void* args) {
    int fd = tfs_open(((args_struct*)args)->path, TFS_O_APPEND);
    assert(fd != -1);
    for (int i = 0; i < COUNT; i++) {
        assert(tfs_write(fd, ((args_struct*)args)->input, SIZE) == SIZE);
    }
    assert(tfs_close(fd) != -1);
    return 0;
}