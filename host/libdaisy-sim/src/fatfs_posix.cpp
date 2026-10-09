/** @file fatfs_posix.cpp
 *  @brief FatFs API on top of the host file system. Volume "0:" is the card
 *  folder. Names resolve case-insensitively like FAT; directory listings are
 *  returned sorted so runs are reproducible.
 */
#define CHOMPI_FF_NO_DIR_ALIAS 1 /* keep FatFs's DIR out of this file: <filesystem> may bring in <dirent.h> */
#include "ff.h"
#include "diskio.h"
#include "sys/fatfs.h"
#include "fatfs_host.h"
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace
{
std::mutex  g_m;
std::string g_root;
bool        g_mounted      = false;
bool        g_card_present = true;

struct DirEntry
{
    std::string name;
    uint32_t    size;
    bool        is_dir;
};

std::string Lower(std::string s)
{
    for(auto& c : s)
        c = char(std::tolower(uint8_t(c)));
    return s;
}

/** Map a FatFs path ("0:/dir/file", "/file", "file") to a host path, resolving
 *  each component case-insensitively against what exists on disk. */
std::string HostPath(const char* path)
{
    std::string p = path ? path : "";
    if(p.size() >= 2 && p[1] == ':')
        p = p.substr(2);
    std::string cur = g_root;
    size_t      i   = 0;
    while(i < p.size())
    {
        while(i < p.size() && (p[i] == '/' || p[i] == '\\'))
            i++;
        size_t j = i;
        while(j < p.size() && p[j] != '/' && p[j] != '\\')
            j++;
        if(j > i)
        {
            std::string comp = p.substr(i, j - i);
            if(comp != ".")
            {
                std::string candidate = cur + "/" + comp;
                std::error_code ec;
                if(!fs::exists(candidate, ec))
                {
                    std::string lc = Lower(comp);
                    for(auto& e : fs::directory_iterator(cur, ec))
                    {
                        if(Lower(e.path().filename().string()) == lc)
                        {
                            candidate = e.path().string();
                            break;
                        }
                    }
                }
                cur = candidate;
            }
        }
        i = j;
    }
    return cur;
}

bool CardOk() { return g_mounted && g_card_present && fs::is_directory(g_root); }

FILE* Fp(FIL* fp) { return fp ? static_cast<FILE*>(fp->sim_fp) : nullptr; }

void FillInfo(FILINFO* fno, const std::string& name, uint32_t size, bool is_dir)
{
    if(!fno)
        return;
    fno->fsize   = size;
    fno->fdate   = 0;
    fno->ftime   = 0;
    fno->fattrib = is_dir ? AM_DIR : AM_ARC;
    std::strncpy(fno->fname, name.c_str(), _MAX_LFN);
    fno->fname[_MAX_LFN] = 0;
    std::strncpy(fno->altname, name.c_str(), 12);
    fno->altname[12] = 0;
}
} // namespace

namespace chompi_sim
{
namespace dev
{
void SetCardRoot(const std::string& root)
{
    std::lock_guard<std::mutex> l(g_m);
    g_root = root;
    while(g_root.size() > 1 && g_root.back() == '/')
        g_root.pop_back();
}
void SetCardPresent(bool present)
{
    std::lock_guard<std::mutex> l(g_m);
    g_card_present = present;
}
} // namespace dev
} // namespace chompi_sim

extern "C"
{
    DSTATUS disk_initialize(BYTE pdrv) { return disk_status(pdrv); }
    DSTATUS disk_status(BYTE pdrv)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(pdrv != 0)
            return STA_NOINIT;
        return (g_card_present && fs::is_directory(g_root)) ? 0 : STA_NODISK;
    }
    DRESULT disk_read(BYTE, BYTE*, DWORD, UINT) { return RES_PARERR; }
    DRESULT disk_write(BYTE, const BYTE*, DWORD, UINT) { return RES_PARERR; }
    DRESULT disk_ioctl(BYTE, BYTE, void*) { return RES_OK; }
    DWORD   get_fattime(void) { return 0; }

    FRESULT f_mount(FATFS* fs, const TCHAR* path, BYTE opt)
    {
        (void)path;
        (void)opt;
        std::lock_guard<std::mutex> l(g_m);
        if(!fs)
        {
            g_mounted = false;
            return FR_OK;
        }
        if(!g_card_present || !fs::is_directory(g_root))
        {
            fs->fs_type = 0;
            return FR_NOT_READY;
        }
        fs->fs_type  = FS_FAT32;
        fs->drv      = 0;
        fs->id       = 1;
        fs->sim_root = &g_root;
        g_mounted    = true;
        return FR_OK;
    }

    FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!fp)
            return FR_INVALID_OBJECT;
        if(!CardOk())
            return FR_NOT_READY;
        if(fp->sim_fp)
        {
            std::fclose(Fp(fp));
            delete static_cast<std::string*>(fp->sim_name);
            fp->sim_fp = nullptr;
            fp->sim_name = nullptr;
        }
        std::string     hp = HostPath(path);
        std::error_code ec;
        bool            exists = fs::is_regular_file(hp, ec);
        if(fs::is_directory(hp, ec))
            return FR_NO_FILE;
        if((mode & FA_CREATE_NEW) && exists)
            return FR_EXIST;
        bool create = mode & (FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS);
        if(!exists && !create)
            return FR_NO_FILE;
        FILE* f = nullptr;
        if((mode & FA_CREATE_ALWAYS) || !exists)
            f = std::fopen(hp.c_str(), "w+b");
        else
            f = std::fopen(hp.c_str(), (mode & FA_WRITE) ? "r+b" : "rb");
        if(!f)
            return FR_DENIED;
        std::fseek(f, 0, SEEK_END);
        long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::memset(fp, 0, sizeof(FIL));
        fp->sim_fp      = f;
        fp->sim_name    = new std::string(hp);
        fp->flag        = mode;
        fp->obj.objsize = size < 0 ? 0 : FSIZE_t(size);
        fp->fptr        = (mode & FA_OPEN_APPEND) == FA_OPEN_APPEND ? fp->obj.objsize : 0;
        return FR_OK;
    }

    FRESULT f_close(FIL* fp)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!fp || !fp->sim_fp)
            return FR_INVALID_OBJECT;
        std::fclose(Fp(fp));
        delete static_cast<std::string*>(fp->sim_name);
        fp->sim_fp   = nullptr;
        fp->sim_name = nullptr;
        return FR_OK;
    }

    FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(br)
            *br = 0;
        if(!fp || !fp->sim_fp)
            return FR_INVALID_OBJECT;
        if(!(fp->flag & FA_READ))
            return FR_DENIED;
        FILE* f = Fp(fp);
        std::fseek(f, long(fp->fptr), SEEK_SET);
        size_t n = std::fread(buff, 1, btr, f);
        fp->fptr += FSIZE_t(n);
        if(br)
            *br = UINT(n);
        return FR_OK;
    }

    FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(bw)
            *bw = 0;
        if(!fp || !fp->sim_fp)
            return FR_INVALID_OBJECT;
        if(!(fp->flag & FA_WRITE))
            return FR_DENIED;
        FILE* f = Fp(fp);
        std::fseek(f, long(fp->fptr), SEEK_SET);
        size_t n = std::fwrite(buff, 1, btw, f);
        fp->fptr += FSIZE_t(n);
        if(fp->fptr > fp->obj.objsize)
            fp->obj.objsize = fp->fptr;
        if(bw)
            *bw = UINT(n);
        return n == btw ? FR_OK : FR_DISK_ERR;
    }

    FRESULT f_lseek(FIL* fp, FSIZE_t ofs)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!fp || !fp->sim_fp)
            return FR_INVALID_OBJECT;
        if(ofs > fp->obj.objsize)
        {
            if(!(fp->flag & FA_WRITE))
                ofs = fp->obj.objsize;
            else
            {
                // FatFs expands the file when seeking past its end in write mode
                FILE* f = Fp(fp);
                std::fflush(f);
                if(ftruncate(fileno(f), off_t(ofs)) == 0)
                    fp->obj.objsize = ofs;
                else
                    ofs = fp->obj.objsize;
            }
        }
        fp->fptr = ofs;
        return FR_OK;
    }

    FRESULT f_truncate(FIL* fp)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!fp || !fp->sim_fp)
            return FR_INVALID_OBJECT;
        if(!(fp->flag & FA_WRITE))
            return FR_DENIED;
        FILE* f = Fp(fp);
        std::fflush(f);
        if(ftruncate(fileno(f), off_t(fp->fptr)) != 0)
            return FR_DISK_ERR;
        fp->obj.objsize = fp->fptr;
        return FR_OK;
    }

    FRESULT f_sync(FIL* fp)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!fp || !fp->sim_fp)
            return FR_INVALID_OBJECT;
        std::fflush(Fp(fp));
        return FR_OK;
    }

    FRESULT f_opendir(FF_DIR* dp, const TCHAR* path)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!dp)
            return FR_INVALID_OBJECT;
        if(!CardOk())
            return FR_NOT_READY;
        std::string     hp = HostPath(path);
        std::error_code ec;
        if(!fs::is_directory(hp, ec))
            return FR_NO_PATH;
        auto* entries = new std::vector<DirEntry>();
        for(auto& e : fs::directory_iterator(hp, ec))
        {
            DirEntry d;
            d.name   = e.path().filename().string();
            d.is_dir = e.is_directory(ec);
            d.size   = d.is_dir ? 0 : uint32_t(e.file_size(ec));
            entries->push_back(d);
        }
        std::sort(entries->begin(), entries->end(), [](const DirEntry& a, const DirEntry& b) { return Lower(a.name) < Lower(b.name); });
        std::memset(dp, 0, sizeof(FF_DIR));
        dp->sim_entries = entries;
        return FR_OK;
    }

    FRESULT f_readdir(FF_DIR* dp, FILINFO* fno)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!dp || !dp->sim_entries)
            return FR_INVALID_OBJECT;
        auto* entries = static_cast<std::vector<DirEntry>*>(dp->sim_entries);
        if(!fno)
        {
            dp->dptr = 0;
            return FR_OK;
        }
        if(dp->dptr >= entries->size())
        {
            fno->fname[0] = 0;
            return FR_OK;
        }
        const DirEntry& e = (*entries)[dp->dptr++];
        FillInfo(fno, e.name, e.size, e.is_dir);
        return FR_OK;
    }

    FRESULT f_closedir(FF_DIR* dp)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!dp || !dp->sim_entries)
            return FR_INVALID_OBJECT;
        delete static_cast<std::vector<DirEntry>*>(dp->sim_entries);
        dp->sim_entries = nullptr;
        return FR_OK;
    }

    FRESULT f_mkdir(const TCHAR* path)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!CardOk())
            return FR_NOT_READY;
        std::string     hp = HostPath(path);
        std::error_code ec;
        if(fs::exists(hp, ec))
            return FR_EXIST;
        return fs::create_directory(hp, ec) ? FR_OK : FR_DENIED;
    }

    FRESULT f_unlink(const TCHAR* path)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!CardOk())
            return FR_NOT_READY;
        std::string     hp = HostPath(path);
        std::error_code ec;
        if(!fs::exists(hp, ec))
            return FR_NO_FILE;
        if(fs::is_directory(hp, ec) && !fs::is_empty(hp, ec))
            return FR_DENIED;
        return fs::remove(hp, ec) ? FR_OK : FR_DENIED;
    }

    FRESULT f_rename(const TCHAR* path_old, const TCHAR* path_new)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!CardOk())
            return FR_NOT_READY;
        std::string     from = HostPath(path_old);
        std::string     to   = HostPath(path_new);
        std::error_code ec;
        if(!fs::exists(from, ec))
            return FR_NO_FILE;
        if(fs::exists(to, ec))
            return FR_EXIST;
        fs::rename(from, to, ec);
        return ec ? FR_DENIED : FR_OK;
    }

    FRESULT f_stat(const TCHAR* path, FILINFO* fno)
    {
        std::lock_guard<std::mutex> l(g_m);
        if(!CardOk())
            return FR_NOT_READY;
        std::string     hp = HostPath(path);
        std::error_code ec;
        if(!fs::exists(hp, ec))
            return FR_NO_FILE;
        bool is_dir = fs::is_directory(hp, ec);
        FillInfo(fno, fs::path(hp).filename().string(), is_dir ? 0 : uint32_t(fs::file_size(hp, ec)), is_dir);
        return FR_OK;
    }

    FRESULT f_chdir(const TCHAR*) { return FR_OK; }
    FRESULT f_getfree(const TCHAR*, DWORD* nclst, FATFS** fatfs)
    {
        if(nclst)
            *nclst = 1000000;
        if(fatfs)
            *fatfs = nullptr;
        return FR_OK;
    }

    int f_putc(TCHAR c, FIL* fp)
    {
        UINT bw = 0;
        return f_write(fp, &c, 1, &bw) == FR_OK && bw == 1 ? 1 : EOF;
    }
    int f_puts(const TCHAR* str, FIL* fp)
    {
        UINT bw = 0, len = UINT(std::strlen(str));
        return f_write(fp, str, len, &bw) == FR_OK ? int(bw) : EOF;
    }
    int f_printf(FIL* fp, const TCHAR* str, ...)
    {
        char    buf[1024];
        va_list va;
        va_start(va, str);
        int n = vsnprintf(buf, sizeof(buf), str, va);
        va_end(va);
        if(n < 0)
            return EOF;
        UINT bw = 0;
        return f_write(fp, buf, UINT(n), &bw) == FR_OK ? int(bw) : EOF;
    }
    TCHAR* f_gets(TCHAR* buff, int len, FIL* fp)
    {
        int  n = 0;
        char c;
        UINT br;
        while(n < len - 1)
        {
            if(f_read(fp, &c, 1, &br) != FR_OK || br != 1)
                break;
            buff[n++] = c;
            if(c == '\n')
                break;
        }
        buff[n] = 0;
        return n ? buff : nullptr;
    }
}

// ---- daisy::FatFSInterface ----
namespace daisy
{
FatFSInterface::Result FatFSInterface::Init(const Config& cfg)
{
    cfg_ = cfg;
    std::strcpy(path_[0], "0:/");
    std::strcpy(path_[1], "1:/");
    std::memset(fs_, 0, sizeof(fs_));
    initialized_ = true;
    return OK;
}
FatFSInterface::Result FatFSInterface::Init(const uint8_t media)
{
    Config c;
    c.media = media;
    return Init(c);
}
FatFSInterface::Result FatFSInterface::DeInit()
{
    initialized_ = false;
    return OK;
}
} // namespace daisy
