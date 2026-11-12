#ifndef TESTS_FATFS_FF_H
#define TESTS_FATFS_FF_H

#include <stdint.h>

typedef unsigned int UINT;
typedef uint64_t FSIZE_t;
typedef enum {
    FR_OK,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE
} FRESULT;
typedef struct {
    uint8_t wflag;
} FATFS;
typedef struct test_fatfs_file {
    struct {
        FATFS *fs;
        FSIZE_t objsize;
    } obj;
    FSIZE_t fptr;
} FIL;
#define FA_READ 1
#define f_size(fil) ((fil)->obj.objsize)
#define f_tell(fil) ((fil)->fptr)
FRESULT f_open (FIL *file, const char *path, uint8_t mode);
FRESULT f_read (FIL *file, void *buffer, UINT bytes, UINT *read);
FRESULT f_lseek (FIL *file, FSIZE_t offset);
FRESULT f_close (FIL *file);

#endif
