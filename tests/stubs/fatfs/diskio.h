#ifndef TESTS_FATFS_DISKIO_H
#define TESTS_FATFS_DISKIO_H

typedef enum {
    RES_OK,
    RES_ERROR,
    RES_WRPRT,
    RES_NOTRDY,
    RES_PARERR
} DRESULT;
#define STA_NOINIT 1
#define CTRL_SYNC 0
#define GET_SECTOR_COUNT 1
#define GET_SECTOR_SIZE 2
#define GET_BLOCK_SIZE 3
#define CTRL_TRIM 4

#endif
