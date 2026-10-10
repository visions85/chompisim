/** @file firmware_select.h
 *  @brief Shared by the launchers and the GUI: which firmware a card folder is
 *  for, which card folder a firmware should get, and where the per-firmware
 *  executables live. Header-only. */
#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace chompi_sim
{

/** What an executable's name ends with on this platform. */
#ifdef _WIN32
constexpr const char* kExeSuffix = ".exe";
#else
constexpr const char* kExeSuffix = "";
#endif

/** The firmwares the simulator knows, in menu order. */
inline const std::vector<std::string>& KnownFirmwares()
{
    static const std::vector<std::string> k = {"wave", "tape", "tempo", "grain"};
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
#if defined(_WIN32)
    char  buf[4096];
    DWORD n = GetModuleFileNameA(nullptr, buf, DWORD(sizeof buf));
    if(n > 0 && n < sizeof buf)
        path.assign(buf, n);
#elif defined(__APPLE__)
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

/** The firmwares in the order the boot window's keys offer them: white key 1
 *  TAPE, 2 WAVE, 3 TEMPO, 4 GRAIN (the slots are fixed, a missing one stays dark). */
inline const std::vector<std::string>& BootOrder()
{
    static const std::vector<std::string> k = {"tape", "wave", "tempo", "grain"};
    return k;
}

/** Every firmware a card folder holds a binary (CHOMPI_WAVE*.bin and friends)
 *  or marker file (CHOMPI_GRAIN.txt) for, in boot order. More than one means a
 *  card made by scripts/make-multi-card.py: TAPE in the root, the others in
 *  WAVE/, TEMPO/ and GRAIN/. */
inline std::vector<std::string> DetectFirmwares(const std::string& card_dir)
{
    std::vector<std::string> found;
    std::error_code          ec;
    for(const auto& e : std::filesystem::directory_iterator(card_dir, ec))
    {
        std::string n = Lower(e.path().filename().string());
        if(n.size() < 4 || (n.substr(n.size() - 4) != ".bin" && n.substr(n.size() - 4) != ".txt") || n.rfind("chompi", 0) != 0)
            continue;
        for(const std::string& fw : KnownFirmwares())
            if(n.find(fw) != std::string::npos && std::find(found.begin(), found.end(), fw) == found.end())
                found.push_back(fw);
    }
    std::vector<std::string> ordered;
    for(const std::string& fw : BootOrder())
        if(std::find(found.begin(), found.end(), fw) != found.end())
            ordered.push_back(fw);
    return ordered;
}

/** The firmware a card folder was made for; the first in boot order when it
 *  holds several; empty when there is none. */
inline std::string DetectFirmware(const std::string& card_dir)
{
    const auto v = DetectFirmwares(card_dir);
    return v.empty() ? "" : v.front();
}

/** What the (simulated) bootloader remembers on a shared card: the firmware
 *  it booted last, in boot_choice.txt in the card's root. */
inline std::string BootChoice(const std::string& card_dir)
{
    std::ifstream f(std::filesystem::path(card_dir) / "boot_choice.txt");
    std::string   s;
    std::getline(f, s);
    while(!s.empty() && (s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    s = Lower(s);
    for(const std::string& fw : KnownFirmwares())
        if(s == fw)
            return fw;
    return "";
}

inline void SaveBootChoice(const std::string& card_dir, const std::string& fw)
{
    std::ofstream f(std::filesystem::path(card_dir) / "boot_choice.txt");
    f << fw << "\n";
}

/** The firmware a shared card boots when no key is held: the remembered one, else the first. */
inline std::string DefaultBoot(const std::string& card_dir)
{
    const auto on_card = DetectFirmwares(card_dir);
    if(on_card.empty())
        return "";
    const std::string last = BootChoice(card_dir);
    return std::find(on_card.begin(), on_card.end(), last) != on_card.end() ? last : on_card.front();
}

/** Firmwares that have an executable `<base>-<fw>` in `dir`. */
inline std::vector<std::string> AvailableFirmwares(const std::string& dir, const std::string& base)
{
    std::vector<std::string> out;
    for(const std::string& fw : KnownFirmwares())
        if(std::filesystem::exists(std::filesystem::path(dir) / (base + "-" + fw + kExeSuffix)))
            out.push_back(fw);
    return out;
}

/** The executable `<dir>/<base>-<fw>` with the platform's suffix. */
inline std::string FirmwareExecutable(const std::string& dir, const std::string& base, const std::string& fw)
{
    return dir + "/" + base + "-" + fw + kExeSuffix;
}

#ifdef _WIN32
/** An argument quoted the way the Windows C runtime parses a command line. */
inline std::string QuoteArgument(const std::string& a)
{
    if(!a.empty() && a.find_first_of(" \t\"") == std::string::npos)
        return a;
    std::string q = "\"";
    size_t      backslashes = 0;
    for(char c : a)
    {
        if(c == '\\')
            backslashes++;
        else if(c == '"')
        {
            q.append(2 * backslashes + 1, '\\'); // every backslash before a quote is doubled, then the quote escaped
            q += '"';
            backslashes = 0;
        }
        else
        {
            q.append(backslashes, '\\');
            q += c;
            backslashes = 0;
        }
    }
    q.append(2 * backslashes, '\\'); // backslashes before the closing quote
    q += '"';
    return q;
}
#endif

/** Runs `args[0]` with those arguments in place of this process: on POSIX the
 *  process is replaced and the call returns only on failure; on Windows the
 *  program is started and, with `wait`, the call returns its exit code, else 0
 *  as soon as it runs. -1 when it could not be started. */
inline int ExecProgram(const std::vector<std::string>& args, bool wait)
{
#ifdef _WIN32
    std::vector<std::string> quoted;
    for(const std::string& a : args)
        quoted.push_back(QuoteArgument(a)); // the C runtime joins them with spaces and no quoting
    std::vector<const char*> argv;
    for(const std::string& q : quoted)
        argv.push_back(q.c_str());
    argv.push_back(nullptr);
    const intptr_t rc = _spawnv(wait ? _P_WAIT : _P_NOWAIT, args[0].c_str(), argv.data());
    if(rc == -1)
        return -1;
    return wait ? int(rc) : 0;
#else
    (void)wait;
    std::vector<std::string> copy = args;
    std::vector<char*>       argv;
    for(std::string& a : copy)
        argv.push_back(&a[0]);
    argv.push_back(nullptr);
    execv(args[0].c_str(), argv.data());
    return -1;
#endif
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
