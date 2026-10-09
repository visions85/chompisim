#include "per/sai.h"
#include "device.h"
using namespace daisy;

class SaiHandle::Impl
{
  public:
    Config cfg;
};

SaiHandle::Result SaiHandle::Init(const Config& config)
{
    static Impl impls[2];
    pimpl_      = &impls[config.periph == Config::Peripheral::SAI_1 ? 0 : 1];
    pimpl_->cfg = config;
    return Result::OK;
}
SaiHandle::Result SaiHandle::DeInit() { return Result::OK; }
const SaiHandle::Config& SaiHandle::GetConfig() const
{
    static Config c;
    return pimpl_ ? pimpl_->cfg : c;
}
SaiHandle::Result SaiHandle::StartDma(int32_t*, int32_t*, size_t, CallbackFunctionPtr) { return Result::OK; }
SaiHandle::Result SaiHandle::StopDma() { return Result::OK; }
float             SaiHandle::GetSampleRate() { return float(chompi_sim::kSampleRate); }
size_t            SaiHandle::GetBlockSize() { return chompi_sim::dev::Device::Get().blocksize; }
float             SaiHandle::GetBlockRate() { return GetSampleRate() / float(GetBlockSize()); }
size_t            SaiHandle::GetOffset() const { return 0; }
