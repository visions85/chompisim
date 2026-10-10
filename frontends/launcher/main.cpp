/** @file main.cpp
 *  @brief Launcher: `chompi-sim` and `chompi-sim-gui` pick the firmware and
 *  run the matching `chompi-sim[-gui]-<firmware>` executable next to them.
 *
 *  The firmware comes from --firmware NAME, else from the firmware binary on
 *  the --card folder (a card with several binaries boots the one chosen last,
 *  else the first in boot order; the GUI then shows its boot window), else
 *  from --cards DIR (the first firmware with a card there), else it is the
 *  first one built. --cards DIR is a folder of card
 *  folders, one per firmware (wave, tape, tempo or wave-1.0 ...); it is
 *  turned into --card for the chosen firmware and passed on as well, so the
 *  GUI can switch firmware later. Every other option is passed through. */
#include "firmware_select.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef LAUNCHER_BASE
#define LAUNCHER_BASE "chompi-sim"
#endif

using namespace chompi_sim;

int main(int argc, char** argv)
{
    std::string              fw, card, cards;
    std::vector<std::string> rest;
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto        value = [&](std::string& out) {
            if(i + 1 >= argc)
            {
                std::fprintf(stderr, "%s needs a value\n", a.c_str());
                std::exit(2);
            }
            out = argv[++i];
        };
        if(a == "--firmware")
            value(fw);
        else if(a == "--card")
            value(card);
        else if(a == "--cards")
            value(cards);
        else
            rest.push_back(a);
    }
    const std::string dir       = ExecutableDir(argv[0]);
    const auto        available = AvailableFirmwares(dir, LAUNCHER_BASE);
    if(available.empty())
    {
        std::fprintf(stderr, "no %s-<firmware> executable next to %s; build the simulator first\n", LAUNCHER_BASE, dir.c_str());
        return 1;
    }
    fw = Lower(fw);
    if(fw.empty() && !card.empty())
        fw = DefaultBoot(card);
    if(fw.empty() && !cards.empty())
        for(const std::string& f : available)
            if(CardForFirmware(cards, "", f) != "")
            {
                fw = f;
                break;
            }
    if(fw.empty())
        fw = available.front();
    const std::string exe = FirmwareExecutable(dir, LAUNCHER_BASE, fw);
    if(!std::filesystem::exists(exe))
    {
        std::fprintf(stderr, "firmware '%s' is not built (no %s). Built:", fw.c_str(), exe.c_str());
        for(const std::string& f : available)
            std::fprintf(stderr, " %s", f.c_str());
        std::fprintf(stderr, "\n");
        return 1;
    }
    if(card.empty())
        card = cards.empty() ? "card" : CardForFirmware(cards, "", fw);
    std::vector<std::string> args = {exe, "--card", card};
    if(!cards.empty())
    {
        args.push_back("--cards");
        args.push_back(cards);
    }
    args.insert(args.end(), rest.begin(), rest.end());
    const int rc = ExecProgram(args, true); // replaces this process, or on Windows waits for the other one
    if(rc < 0)
    {
        std::perror(exe.c_str());
        return 1;
    }
    return rc;
}
