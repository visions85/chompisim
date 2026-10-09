/** @file firmware_select.h
 *  @brief Shared by the launchers and the GUI: which firmware a card folder is
 *  for, which card folder a firmware should get, and where the per-firmware
 *  executables live. Header-only. */
#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace chompi_sim
{

/** The firmwares the simulator knows, in menu order. */
inline const std::vector<std::string>& KnownFirmwares()
{
    static const std::vector<std::string> k = {"wave", "tape", "tempo"};
    return k;
}

inline std::string Lower(std::string s)
{
    for(char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/** Folder of the running executable (empty if unknown). */
inline std::string ExecutableDir(const char* argv0)
{
    std::string path;
#ifdef __APPLE__
    char     buf[4096];
    uint32_t n = sizeof buf;
    if(_NSGetExecutablePath(buf, &n) == 0)
        path = buf;
#else
    char    buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if(n > 0)
        path.assign(buf, size_t(n));
#endif
    if(path.empty() && argv0)
        path = argv0;
    std::error_code ec;
    auto            p = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    if(ec)
        p = std::filesystem::absolute(std::filesystem::path(path), ec);
    return p.parent_path().string();
}

/** The firmware a card folder was made for, from the firmware binary on it
 *  (CHOMPI_WAVE*.bin and friends); empty when there is none. */
inline std::string DetectFirmware(const std::string& card_dir)
{
    std::error_code ec;
    for(const auto& e : std::filesystem::directory_iterator(card_dir, ec))
    {
        std::string n = Lower(e.path().filename().string());
        if(n.size() < 4 || n.substr(n.size() - 4) != ".bin")
            continue;
        for(const std::string& fw : KnownFirmwares())
            if(n.find(fw) != std::string::npos)
                return fw;
    }
    return "";
}

/** Firmwares that have an executable `<base>-<fw>` in `dir`. */
inline std::vector<std::string> AvailableFirmwares(const std::string& dir, const std::string& base)
{
    std::vector<std::string> out;
    for(const std::string& fw : KnownFirmwares())
        if(std::filesystem::exists(std::filesystem::path(dir) / (base + "-" + fw)))
            out.push_back(fw);
    return out;
}

/** The card folder to boot firmware `fw` with: `<cards>/<fw>` or `<cards>/<fw>-*`
 *  when a cards folder is given, else the current card if it is for `fw`, else
 *  a sibling of the current card named like that; otherwise the current card. */
inline std::string CardForFirmware(const std::string& cards_dir, const std::string& current_card, const std::string& fw)
{
    auto look = [&](const std::filesystem::path& parent) -> std::string {
        std::error_code ec;
        if(std::filesystem::is_directory(parent / fw, ec))
            return (parent / fw).string();
        std::vector<std::string> hits;
        for(const auto& e : std::filesystem::directory_iterator(parent, ec))
            if(e.is_directory(ec) && Lower(e.path().filename().string()).rfind(fw + "-", 0) == 0)
                hits.push_back(e.path().string());
        std::sort(hits.begin(), hits.end());
        if(!hits.empty())
            return hits.back(); // the newest version
        for(const auto& e : std::filesystem::directory_iterator(parent, ec))
            if(e.is_directory(ec) && DetectFirmware(e.path().string()) == fw)
                return e.path().string();
        return "";
    };
    if(!cards_dir.empty())
    {
        std::string c = look(cards_dir);
        if(!c.empty())
            return c;
    }
    if(DetectFirmware(current_card) == fw)
        return current_card;
    std::error_code ec;
    auto            parent = std::filesystem::absolute(current_card, ec).parent_path();
    std::string     c      = look(parent);
    return c.empty() ? current_card : c;
}

} // namespace chompi_sim
