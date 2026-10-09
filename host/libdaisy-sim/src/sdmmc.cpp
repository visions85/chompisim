#include "per/sdmmc.h"
using namespace daisy;
SdmmcHandler::Result SdmmcHandler::Init(const Config& cfg)
{
    (void)cfg;
    return Result::OK;
}
