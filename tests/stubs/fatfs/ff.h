#ifndef TESTS_FATFS_FF_H
#define TESTS_FATFS_FF_H

#include <stdint.h>

#define FF_CODE_PAGE 437
#define FF_LFN_UNICODE 2
#define FF_USE_LFN 1
#define FF_SFN_BUF 12
#define FF_LFN_BUF 255

typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint16_t WCHAR;
typedef unsigned int UINT;
typedef uint64_t FSIZE_t;
typedef enum {
    FR_OK,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME
} FRESULT;
typedef struct {
    uint8_t wflag;
} FATFS;
typedef struct {
    uint8_t fattrib;
    char altname[FF_SFN_BUF + 1];
    char fname[FF_LFN_BUF + 1];
    FSIZE_t fsize;
} FILINFO;

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
FRESULT f_stat (const char *path, FILINFO *info);
WCHAR ff_uni2oem (DWORD unicode, WORD codepage);
FRESULT f_open (FIL *file, const char *path, uint8_t mode);
FRESULT f_read (FIL *file, void *buffer, UINT bytes, UINT *read);
FRESULT f_lseek (FIL *file, FSIZE_t offset);
FRESULT f_close (FIL *file);

#endif
