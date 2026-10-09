/*----------------------------------------------------------------------------/
/  FatFs API (R0.12c compatible subset) implemented on the host file system.  /
/  The CHOMPI simulator maps volume "0:" to a folder that plays the microSD.  /
/  Original FatFs: Copyright (C) 2017, ChaN. This is a re-implementation of   /
/  the public interface only.                                                 /
/----------------------------------------------------------------------------*/
#ifndef _FATFS
#define _FATFS 68300
#ifdef __cplusplus
extern "C" {
#endif
#include "integer.h"
#include "ffconf.h"

typedef char  TCHAR;
#define _T(x) x
#define _TEXT(x) x
typedef DWORD FSIZE_t;

/* File system object */
typedef struct {
    BYTE  fs_type;   /* non-zero when mounted */
    BYTE  drv;
    WORD  id;
    void* sim_root;  /* simulator: const std::string* with the host folder */
} FATFS;

/* Object ID (mirrors the fields firmware code reads: obj.objsize) */
typedef struct {
    FATFS*  fs;
    WORD    id;
    BYTE    attr;
    BYTE    stat;
    DWORD   sclust;
    FSIZE_t objsize;
} _FDID;

/* File object. Firmware code reads fptr and obj.objsize directly (f_tell / f_size). */
typedef struct {
    _FDID   obj;
    BYTE    flag;
    BYTE    err;
    FSIZE_t fptr;
    DWORD   clust;
    DWORD   sect;
    void*   sim_fp;   /* simulator: FILE* */
    void*   sim_name; /* simulator: std::string* with the host path */
} FIL;

/* Directory object */
typedef struct {
    _FDID obj;
    DWORD dptr;
    DWORD clust;
    DWORD sect;
    void* sim_entries; /* simulator: std::vector<...>* */
} DIR;

/* File information */
typedef struct {
    FSIZE_t fsize;
    WORD    fdate;
    WORD    ftime;
    BYTE    fattrib;
    TCHAR   altname[13];
    TCHAR   fname[_MAX_LFN + 1];
} FILINFO;

typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH,
    FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM, FR_MKFS_ABORTED, FR_TIMEOUT,
    FR_LOCKED, FR_NOT_ENOUGH_CORE, FR_TOO_MANY_OPEN_FILES, FR_INVALID_PARAMETER
} FRESULT;

FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode);
FRESULT f_close(FIL* fp);
FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br);
FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw);
FRESULT f_lseek(FIL* fp, FSIZE_t ofs);
FRESULT f_truncate(FIL* fp);
FRESULT f_sync(FIL* fp);
FRESULT f_opendir(DIR* dp, const TCHAR* path);
FRESULT f_closedir(DIR* dp);
FRESULT f_readdir(DIR* dp, FILINFO* fno);
FRESULT f_mkdir(const TCHAR* path);
FRESULT f_unlink(const TCHAR* path);
FRESULT f_rename(const TCHAR* path_old, const TCHAR* path_new);
FRESULT f_stat(const TCHAR* path, FILINFO* fno);
FRESULT f_chdir(const TCHAR* path);
FRESULT f_getfree(const TCHAR* path, DWORD* nclst, FATFS** fatfs);
FRESULT f_mount(FATFS* fs, const TCHAR* path, BYTE opt);
int     f_putc(TCHAR c, FIL* fp);
int     f_puts(const TCHAR* str, FIL* cp);
int     f_printf(FIL* fp, const TCHAR* str, ...);
TCHAR*  f_gets(TCHAR* buff, int len, FIL* fp);

#define f_eof(fp) ((int)((fp)->fptr == (fp)->obj.objsize))
#define f_error(fp) ((fp)->err)
#define f_tell(fp) ((fp)->fptr)
#define f_size(fp) ((fp)->obj.objsize)
#define f_rewind(fp) f_lseek((fp), 0)
#define f_rewinddir(dp) f_readdir((dp), 0)
#define f_rmdir(path) f_unlink(path)
#define f_unmount(path) f_mount(0, path, 0)
#ifndef EOF
#define EOF (-1)
#endif

DWORD get_fattime(void);

#define FA_READ          0x01
#define FA_WRITE         0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_NEW    0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS   0x10
#define FA_OPEN_APPEND   0x30

#define FM_FAT   0x01
#define FM_FAT32 0x02
#define FM_EXFAT 0x04
#define FM_ANY   0x07
#define FM_SFD   0x08

#define FS_FAT12 1
#define FS_FAT16 2
#define FS_FAT32 3
#define FS_EXFAT 4

#define AM_RDO 0x01
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_DIR 0x10
#define AM_ARC 0x20

#ifdef __cplusplus
}
#endif
#endif /* _FATFS */
