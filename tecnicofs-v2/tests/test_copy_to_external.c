#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>

#define COUNT 40
#define DOUBLECOUNT 80
#define SIZE 256

/**
   This test performs the copy_to_external from two
   different files of tecnicofs to two different
   external files.
 */

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
    char *path4="/f2";
    //char to_read[40];

    assert(tfs_init() != -1);

    int file1 = tfs_open(path1, TFS_O_CREAT);
    assert(file1 != -1);

    assert(tfs_write(file1, str, strlen(str)) != -1);

    int file2 = tfs_open(path4, TFS_O_CREAT);
    assert(file2 != -1);

    assert(tfs_write(file2, str, strlen(str)) != -1);

    pthread_t cpy1, cpy2, cpy3, cpy4;
    args_struct args1,args2,args3,args4;
    args1.path_o=path1;
    args2.path_o=path1;
    args1.path_d=path2;
    args2.path_d=path3;
    args3.path_o=path4;
    args4.path_o=path4;
    args3.path_d=path2;
    args4.path_d=path3;

    args1.str=str;
    args2.str=str;
    args3.str=str;
    args4.str=str;

    assert(pthread_create(&cpy1,NULL, &copy_to_external, &args1) == 0);
    assert(pthread_create(&cpy2,NULL, &copy_to_external, &args2) == 0);
    assert(pthread_create(&cpy3,NULL, &copy_to_external, &args3) == 0);
    assert(pthread_create(&cpy4,NULL, &copy_to_external, &args4) == 0);
    assert(pthread_join(cpy1, NULL) == 0);
    assert(pthread_join(cpy2, NULL) == 0);
    assert(pthread_join(cpy3, NULL) == 0);
    assert(pthread_join(cpy4, NULL) == 0);

    assert(tfs_close(file1) != -1);
    assert(tfs_close(file2) != -1);


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


