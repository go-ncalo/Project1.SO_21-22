#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>

#define COUNT 40
#define DOUBLECOUNT 80
#define SIZE 256

typedef struct {
    char *path_o;
    char *path_d;
    char *str;
} args_struct;

void* copy_to_external (void* args);

int main() {

    char *str = "AAA! AAA! AAA! ";
    char *path1 = "/f1";
    char *path2 = "external_file1.txt";
    char *path3 = "external_file2.txt";
    //char to_read[40];

    assert(tfs_init() != -1);

    int file = tfs_open(path1, TFS_O_CREAT);
    assert(file != -1);

    assert(tfs_write(file, str, strlen(str)) != -1);

    pthread_t cpy1, cpy2;
    args_struct args1,args2;
    args1.path_o=path1;
    args2.path_o=path1;
    args1.path_d=path2;
    args2.path_d=path3;
    args1.str=str;
    args2.str=str;

    assert(pthread_create(&cpy1,NULL, &copy_to_external, &args1) == 0);
    assert(pthread_create(&cpy2,NULL, &copy_to_external, &args2) == 0);
    assert(pthread_join(cpy1, NULL) == 0);
    assert(pthread_join(cpy2, NULL) == 0);

    assert(tfs_close(file) != -1);


    unlink(path2);

    unlink(path3);

    printf("Successful test.\n");

    return 0;
}

void* copy_to_external (void* args) {
    char to_read[40];

    assert(tfs_copy_to_external_fs(((args_struct*) args)->path_o,((args_struct*) args)->path_d) != -1);

    FILE *fp = fopen(((args_struct*) args)->path_d, "r");

    assert(fp != NULL);

    assert(fread(to_read, sizeof(char), strlen(((args_struct*) args)->str), fp) == strlen(((args_struct*) args)->str));
    
    assert(strcmp((((args_struct*) args)->str), to_read) == 0);

    assert(fclose(fp) != -1);
    return 0;
}


