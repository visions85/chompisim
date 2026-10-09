#ifndef __fatfs_H
#define __fatfs_H
#include "ff.h"
namespace daisy
{
/** Links FatFs to the simulated media. The SD volume is the card folder. */
class FatFSInterface
{
  public:
    enum Result { OK, ERR_TOO_MANY_VOLUMES, ERR_NO_MEDIA_SELECTED, ERR_GENERIC };
    struct Config
    {
        enum Media : uint8_t { MEDIA_SD = 0x01, MEDIA_USB = 0x02 };
        uint8_t media;
    };
    FatFSInterface() : initialized_(false) {}
    Result        Init(const Config& cfg);
    Result        Init(const uint8_t media);
    Result        DeInit();
    bool          Initialized() const { return initialized_; }
    const Config& GetConfig() const { return cfg_; }
    Config&       GetMutableConfig() { return cfg_; }
    const char*   GetSDPath() const { return path_[0]; }
    const char*   GetUSBPath() const { return path_[1]; }
    FATFS&        GetSDFileSystem() { return fs_[0]; }
    FATFS&        GetUSBFileSystem() { return fs_[1]; }

  private:
    Config cfg_;
    FATFS  fs_[_VOLUMES];
    char   path_[_VOLUMES][4];
    bool   initialized_;
};
} // namespace daisy
extern "C" DWORD get_fattime(void);
#endif
