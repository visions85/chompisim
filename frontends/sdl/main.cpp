/** @file main.cpp
 *  @brief SDL2 desktop front-end for the CHOMPI simulator: command line,
 *  window, sound card, input mapping and the 60 fps event loop. */
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include "chompi_sim/sim.h"
#include "firmware_info.h"
#include "firmware_select.h"
#include "panel.h"
#include "tour.h"

#ifndef CHOMPI_SIM_FIRMWARE
#define CHOMPI_SIM_FIRMWARE ""
#endif

using namespace chompi_sim;

namespace
{

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------
struct Options
{
    std::string card       = "card";
    bool        no_audio   = false;
    std::string screenshot;      /**< BMP written after screenshot_at seconds */
    double      screenshot_at = 1.5;
    double      exit_after = -1; /**< seconds, < 0 = run until closed */
    float       scale      = 1.f;
    int         pair       = 1; /**< output pair for the sound card: 0 hp, 1 line */
    bool        keymap     = true; /**< start with the key map overlay shown */
    std::string input;              /**< WAV to play into the inputs */
    bool        input_loop = false;
    float       input_gain = 1.f;
    int         line_in    = -1;    /**< aux jack: 1 plugged, 0 not, -1 plugged while a sound is loaded */
    bool        mic        = false; /**< feed the computer's microphone */
    std::string cards;              /**< folder of card folders, one per firmware (for switching) */
    bool        tour    = false;    /**< show the guided tour at start */
    bool        no_tour = false;    /**< never show it by itself */
    bool        booted  = false;    /**< the boot window already chose this firmware (set when it relaunches) */
    std::string argv0;
};

constexpr double kBootWindowS = 2.5; /**< the bootloader's window: how long a shared card waits for a key */

constexpr float  kDegreesPerDetent = 15.f; /**< 24 detents per turn */
constexpr float  kDragPixelsPerDetent = 6.f; /**< mouse drag distance per detent, logical pixels */
constexpr int    kAudioRate   = 48000;
constexpr int    kAudioFrames = 256;

void PrintUsage(const char* argv0)
{
    std::printf("usage: %s [options]\n"
                "  --card <dir>          folder acting as the microSD card (default: card)\n"
                "  --no-audio            do not open a sound card, run on the null audio clock\n"
                "  --screenshot <f.bmp>  save the window to a BMP (after --screenshot-at seconds)\n"
                "  --screenshot-at <sec> when to take the screenshot (default 1.5)\n"
                "  --exit-after <sec>    quit after that many seconds\n"
                "  --scale <float>       window scale (default 1.0)\n"
                "  --pair <0|1>          output pair for the sound card: 0 headphones, 1 line out (default 1)\n"
                "  --no-keymap           start without the key map overlay (/ or ? toggles it)\n"
                "  --input <file.wav>    sound to play into the inputs: F7 plays it, F8 stops it;\n"
                "                        dropping a WAV onto the window loads and plays it too\n"
                "  --loop                loop the input sound\n"
                "  --gain <float>        input sound level (default 1.0)\n"
                "  --line-in | --mic-in  aux jack plugged or not (default: plugged while a sound is loaded)\n"
                "  --mic                 feed the computer's microphone into the inputs\n"
                "  --cards <dir>         folder of card folders (wave, tape, tempo or wave-1.0 ...): the firmware\n"
                "                        tabs in the bar switch to the matching card; --firmware is accepted too\n"
                "  --tour                show the guided tour of the panel at start (it shows by itself the first\n"
                "                        time a firmware runs; the ? in the bar shows it any time)\n"
                "  --no-tour             never show the tour by itself\n"
                "  --booted              skip the boot window of a card that holds several firmwares (the\n"
                "                        window passes this when it relaunches into the chosen one)\n"
                "  --help                this text\n",
                argv0);
}

/** Returns 0 to run, 1 on error, 2 when --help was printed. */
int ParseArgs(int argc, char** argv, Options& o)
{
    o.argv0 = argv[0];
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto        value = [&](const char*& out) {
            if(i + 1 >= argc)
            {
                std::fprintf(stderr, "%s needs a value\n", a.c_str());
                return false;
            }
            out = argv[++i];
            return true;
        };
        const char* v = nullptr;
        if(a == "--help" || a == "-h")
        {
            PrintUsage(argv[0]);
            return 2;
        }
        else if(a == "--no-audio")
            o.no_audio = true;
        else if(a == "--no-keymap")
            o.keymap = false;
        else if(a == "--input")
        {
            if(!value(v))
                return 1;
            o.input = v;
        }
        else if(a == "--loop")
            o.input_loop = true;
        else if(a == "--gain")
        {
            if(!value(v))
                return 1;
            o.input_gain = float(std::atof(v));
        }
        else if(a == "--line-in")
            o.line_in = 1;
        else if(a == "--mic-in")
            o.line_in = 0;
        else if(a == "--mic")
            o.mic = true;
        else if(a == "--tour")
            o.tour = true;
        else if(a == "--no-tour")
            o.no_tour = true;
        else if(a == "--booted")
            o.booted = true;
        else if(a == "--cards")
        {
            if(!value(v))
                return 1;
            o.cards = v;
        }
        else if(a == "--firmware")
        {
            if(!value(v)) // the launcher picked this executable; nothing to do here
                return 1;
        }
        else if(a == "--card")
        {
            if(!value(v))
                return 1;
            o.card = v;
        }
        else if(a == "--screenshot")
        {
            if(!value(v))
                return 1;
            o.screenshot = v;
        }
        else if(a == "--screenshot-at")
        {
            if(!value(v))
                return 1;
            o.screenshot_at = std::atof(v);
        }
        else if(a == "--exit-after")
        {
            if(!value(v))
                return 1;
            o.exit_after = std::atof(v);
        }
        else if(a == "--scale")
        {
            if(!value(v))
                return 1;
            o.scale = float(std::atof(v));
            if(!(o.scale > 0.05f && o.scale < 20.f))
            {
                std::fprintf(stderr, "--scale must be a positive number\n");
                return 1;
            }
        }
        else if(a == "--pair")
        {
            if(!value(v))
                return 1;
            o.pair = std::atoi(v);
            if(o.pair != 0 && o.pair != 1)
            {
                std::fprintf(stderr, "--pair must be 0 (headphones) or 1 (line out)\n");
                return 1;
            }
        }
        else
        {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            PrintUsage(argv[0]);
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Keyboard mapping
// ---------------------------------------------------------------------------

/** The computer keyboard as a piano, laid out like a DAW's: the home row plays
 *  the white keys (the lower row of caps) and the row above it the black keys,
 *  from the low C to the F an octave and a fourth up. Physical key positions
 *  (scancodes), so the rows hold on any keyboard layout; z and x move the span
 *  down and up an octave to reach the top caps. */
struct PianoKeyMap
{
    SDL_Scancode key;
    int          semitone; /**< within the span, 0 = the low C */
};
constexpr PianoKeyMap kPianoMap[gui::kPianoSpan] = {
    {SDL_SCANCODE_A, 0},  {SDL_SCANCODE_W, 1},  {SDL_SCANCODE_S, 2},  {SDL_SCANCODE_E, 3},  {SDL_SCANCODE_D, 4},
    {SDL_SCANCODE_F, 5},  {SDL_SCANCODE_T, 6},  {SDL_SCANCODE_G, 7},  {SDL_SCANCODE_Y, 8},  {SDL_SCANCODE_H, 9},
    {SDL_SCANCODE_U, 10}, {SDL_SCANCODE_J, 11}, {SDL_SCANCODE_K, 12}, {SDL_SCANCODE_O, 13}, {SDL_SCANCODE_L, 14},
    {SDL_SCANCODE_P, 15}, {SDL_SCANCODE_SEMICOLON, 16}, {SDL_SCANCODE_APOSTROPHE, 17},
};
constexpr SDL_Scancode kOctaveDownKey = SDL_SCANCODE_Z, kOctaveUpKey = SDL_SCANCODE_X;

/** The boot window of a shared card: the first four white keys (a s d f, caps 1 to 4) pick the firmware. */
constexpr SDL_Scancode kBootKeys[4]      = {SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F};
constexpr int          kBootSemitones[4] = {0, 2, 4, 5};

int PianoSpanSemitone(SDL_Scancode sc)
{
    for(const PianoKeyMap& m : kPianoMap)
        if(m.key == sc)
            return m.semitone;
    return -1;
}

/** Keys that turn a knob; these may auto-repeat. Returns false if `k` is not one. */
bool TurnKey(SDL_Keycode k, int lastSmallKnob, int& enc, int& detents)
{
    switch(k)
    {
        case SDLK_LEFTBRACKET: enc = ENC_SW5; detents = -1; return true;
        case SDLK_RIGHTBRACKET: enc = ENC_SW5; detents = +1; return true;
        case SDLK_MINUS: enc = ENC_SW6; detents = -1; return true;
        case SDLK_EQUALS: enc = ENC_SW6; detents = +1; return true;
        case SDLK_LEFT: enc = lastSmallKnob; detents = -1; return true;
        case SDLK_RIGHT: enc = lastSmallKnob; detents = +1; return true;
        default: return false;
    }
}

bool IsSmallKnob(int enc)
{
    return enc == ENC_SW1 || enc == ENC_SW2 || enc == ENC_SW3 || enc == ENC_SW4;
}

// ---------------------------------------------------------------------------
// Input bookkeeping: a control stays pressed while either the keyboard or the
// mouse holds it.
// ---------------------------------------------------------------------------
class Input
{
  public:
    void KeyboardButton(int b, bool down)
    {
        kb_btn_[b] = down;
        Sim::Get().SetButton(b, kb_btn_[b] || mouse_btn_[b]);
    }
    void MouseButton(int b, bool down)
    {
        mouse_btn_[b] = down;
        Sim::Get().SetButton(b, kb_btn_[b] || mouse_btn_[b]);
    }
    void KeyboardEncoder(int e, bool down)
    {
        kb_enc_[e] = down;
        Sim::Get().SetEncoderPressed(e, EncoderPressed(e));
    }
    void MouseEncoder(int e, bool down)
    {
        mouse_enc_[e] = down;
        Sim::Get().SetEncoderPressed(e, EncoderPressed(e));
    }
    bool EncoderPressed(int e) const { return kb_enc_[e] || mouse_enc_[e]; }
    /** Lets go of everything the keyboard holds (used when the window loses focus). */
    void ReleaseKeyboard()
    {
        for(int b = 0; b < NUM_BUTTONS; b++)
            if(kb_btn_[b])
                KeyboardButton(b, false);
        for(int e = 0; e < kNumEncoders; e++)
            if(kb_enc_[e])
                KeyboardEncoder(e, false);
    }

  private:
    bool kb_btn_[NUM_BUTTONS]    = {};
    bool mouse_btn_[NUM_BUTTONS] = {};
    bool kb_enc_[kNumEncoders]    = {};
    bool mouse_enc_[kNumEncoders] = {};
};

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
void AudioCallback(void* userdata, Uint8* stream, int len)
{
    int pair = *static_cast<int*>(userdata);
    Sim::Get().RenderStereo(reinterpret_cast<float*>(stream), size_t(len) / (2 * sizeof(float)), pair);
}

/** Host microphone frames (mono float) go to the simulated inputs. */
void CaptureCallback(void*, Uint8* stream, int len)
{
    Sim::Get().PushInput(reinterpret_cast<const float*>(stream), size_t(len) / sizeof(float));
}

/** A native "choose a file" dialog: AppleScript on macOS, zenity or kdialog
 *  elsewhere. Returns the path, or "" with `why` set when the dialog was
 *  cancelled or none is available. The event loop waits meanwhile; audio and
 *  the firmware keep running on their own threads. */
std::string PickSoundFile(std::string& why)
{
    const char* const cmds[] = {
#ifdef __APPLE__
        "osascript -e 'tell application \"System Events\"' -e 'activate' "
        "-e 'set f to choose file with prompt \"Sound to play into the inputs\" of type {\"public.audio\"}' "
        "-e 'POSIX path of f' -e 'end tell' 2>/dev/null",
#else
        "zenity --file-selection --title='Sound to play into the inputs' --file-filter='Sounds | *.wav *.WAV' 2>/dev/null",
        "kdialog --getopenfilename . 'Sounds (*.wav *.WAV)' 2>/dev/null",
#endif
    };
    for(const char* cmd : cmds)
    {
        FILE* p = popen(cmd, "r");
        if(!p)
            continue;
        std::string out;
        char        buf[4096];
        while(std::fgets(buf, sizeof buf, p))
            out += buf;
        const int rc     = pclose(p);
        const int status = WIFEXITED(rc) ? WEXITSTATUS(rc) : 255;
        while(!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        if(status == 0 && !out.empty())
            return out;
        if(status <= 1) // the dialog ran and was dismissed
        {
            why = "cancelled";
            return "";
        }
        // 127: no such program, try the next one
    }
    why = "no file dialog available (zenity or kdialog); drop a WAV onto the window or start with --input";
    return "";
}

bool SaveScreenshot(SDL_Renderer* r, const std::string& path)
{
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if(!s)
        return false;
    bool ok = SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0
              && SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}

// ---------------------------------------------------------------------------
// The sounds on the card, listed the way the GRAIN firmware loads them, so a
// row of the sound menu is the sound of that number in the firmware
// ---------------------------------------------------------------------------

/** True for a WAV the firmware takes: 48 kHz 16-bit PCM, mono or stereo, the
 *  data chunk inside the first 4 KB (GrainEngine.h, ReadWavInfo). */
bool GrainWavOk(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    unsigned char head[4096];
    f.read(reinterpret_cast<char*>(head), sizeof head);
    const size_t br = size_t(std::max<std::streamsize>(f.gcount(), 0));
    if(br < 12 || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0)
        return false;
    auto u16 = [&](size_t at) { return unsigned(head[at]) | unsigned(head[at + 1]) << 8; };
    auto u32 = [&](size_t at) { return uint32_t(u16(at)) | uint32_t(u16(at + 2)) << 16; };
    unsigned format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    bool     data = false;
    for(size_t pos = 12; pos + 8 <= br;)
    {
        const uint32_t len = u32(pos + 4);
        if(std::memcmp(head + pos, "fmt ", 4) == 0 && len >= 16 && pos + 24 <= br)
        {
            format   = u16(pos + 8);
            channels = u16(pos + 10);
            rate     = u32(pos + 12);
            bits     = u16(pos + 22);
            if(format == 0xFFFE && len >= 26 && pos + 34 <= br)
                format = u16(pos + 32);
        }
        else if(std::memcmp(head + pos, "data", 4) == 0)
        {
            data = true;
            break;
        }
        pos += 8 + size_t(len) + (len & 1);
    }
    return data && format == 1 && bits == 16 && rate == 48000 && (channels == 1 || channels == 2);
}

/** The card's sounds in the firmware's order: the first `limit` .wav files by
 *  name (TAPE's _double copies left out) with a header the firmware accepts. */
std::vector<gui::SoundEntry> ScanCardSounds(const std::string& dir, int limit)
{
    std::vector<std::string> names;
    std::error_code          ec;
    for(const auto& e : std::filesystem::directory_iterator(dir, ec))
    {
        const std::string n = e.path().filename().string();
        if(n.size() < 5 || n[0] == '.' || n.find("_double") != std::string::npos)
            continue;
        std::string ext = n.substr(n.size() - 4);
        for(char& c : ext)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        if(ext == ".wav")
            names.push_back(n);
    }
    std::sort(names.begin(), names.end());
    std::vector<gui::SoundEntry> out;
    for(const std::string& n : names)
    {
        if(int(out.size()) >= limit)
            break;
        if(GrainWavOk(std::filesystem::path(dir) / n))
            out.push_back({int(out.size()), n.substr(0, n.size() - 4)});
    }
    return out;
}

std::array<int, SDL_NUM_SCANCODES> MakeHeldButtons()
{
    std::array<int, SDL_NUM_SCANCODES> a;
    a.fill(-1);
    return a;
}

// ---------------------------------------------------------------------------
// The application
// ---------------------------------------------------------------------------
class App
{
  public:
    explicit App(const Options& o) : opt_(o), pair_(o.pair)
    {
        ui_.keymap    = o.keymap;
        ui_.firmware  = CHOMPI_SIM_FIRMWARE;
        ui_.card_name = std::filesystem::path(o.card).filename().string();
        exe_dir_      = ExecutableDir(o.argv0.c_str());
        ui_.firmwares_built = AvailableFirmwares(exe_dir_, "chompi-sim-gui");

        // a card that holds several firmwares: the boot window picks one (scripts/make-multi-card.py)
        on_card_    = DetectFirmwares(o.card);
        boot_phase_ = on_card_.size() > 1 && !o.booted;
        for(size_t i = 0; i < ui_.boot_slots.size() && i < BootOrder().size(); i++)
        {
            const std::string& id = BootOrder()[i];
            if(std::find(on_card_.begin(), on_card_.end(), id) != on_card_.end())
                ui_.boot_slots[i] = gui::FirmwareByName(id).name[0] ? gui::FirmwareByName(id).name : id;
        }
        ui_.boot_default = gui::FirmwareByName(ui_.firmware).name; // no key: the one the launcher picked

        // the sound menu, for a firmware that selects sounds over MIDI
        const gui::FirmwareInfo& fw = gui::FirmwareByName(ui_.firmware);
        sound_cc_                   = fw.sound_cc;
        if(sound_cc_)
        {
            ui_.sounds = ScanCardSounds(o.card, fw.record_sound >= 0 ? fw.record_sound : 128);
            if(fw.record_sound >= 0)
                ui_.sounds.push_back({fw.record_sound, "recording (CHOMPI key)"});
        }
    }

    /** Reboots into another firmware: the matching executable next to this
     *  one replaces the process, with the card for that firmware and the
     *  same options otherwise. */
    void SwitchFirmware(int index)
    {
        const gui::FirmwareInfo& f = gui::kFirmwares[index];
        if(ui_.firmware == f.id)
            return;
        if(std::find(ui_.firmwares_built.begin(), ui_.firmwares_built.end(), f.id) == ui_.firmwares_built.end())
        {
            log_.push_back(std::string(f.name) + " is not built (no chompi-sim-gui-" + f.id + " next to this executable)");
            return;
        }
        const bool  shared = std::find(on_card_.begin(), on_card_.end(), f.id) != on_card_.end() && on_card_.size() > 1;
        std::string card   = shared ? opt_.card : CardForFirmware(opt_.cards, opt_.card, f.id);
        if(!shared && DetectFirmware(card) != f.id)
            std::fprintf(stderr, "no card folder for %s found; booting it with %s\n", f.name, card.c_str());
        std::vector<std::string> args = {exe_dir_ + "/chompi-sim-gui-" + f.id, "--card", card, "--pair", std::to_string(pair_),
                                         "--scale", std::to_string(opt_.scale), "--gain", std::to_string(opt_.input_gain)};
        if(shared)
        {
            SaveBootChoice(opt_.card, f.id); // what the bootloader would remember
            args.push_back("--booted");
        }
        if(!opt_.cards.empty())
            args.insert(args.end(), {"--cards", opt_.cards});
        if(opt_.no_audio)
            args.push_back("--no-audio");
        if(!ui_.keymap)
            args.push_back("--no-keymap");
        if(!opt_.input.empty())
            args.insert(args.end(), {"--input", opt_.input});
        if(opt_.input_loop)
            args.push_back("--loop");
        if(opt_.mic)
            args.push_back("--mic");
        if(opt_.line_in == 1)
            args.push_back("--line-in");
        else if(opt_.line_in == 0)
            args.push_back("--mic-in");
        if(opt_.tour)
            args.push_back("--tour");
        if(opt_.no_tour)
            args.push_back("--no-tour");
        std::fprintf(stderr, "switching to %s: %s --card %s\n", f.name, args[0].c_str(), card.c_str());
        Shutdown();
        std::vector<char*> cargs;
        for(std::string& a : args)
            cargs.push_back(&a[0]);
        cargs.push_back(nullptr);
        execv(args[0].c_str(), cargs.data());
        std::perror(args[0].c_str());
        std::exit(1);
    }
    ~App() { Shutdown(); }

    int Run()
    {
        if(!InitSim())
            return 1;
        if(!InitVideo())
            return 1;
        InitAudio();
        if(boot_phase_)
            BeginBoot();
        else
            MaybeStartTour();
        Loop();
        return 0;
    }

    void MaybeStartTour()
    {
        if(opt_.tour || (!opt_.no_tour && !TourSeen()))
            StartTour();
    }

    // ---- the boot window of a shared card: what the bootloader would do with a key held at power-on ----

    void BeginBoot()
    {
        boot_until_     = kBootWindowS; // Loop() counts from its start
        ui_.boot_window = true;
        NewBootColour();
        std::string keys;
        for(size_t i = 0; i < ui_.boot_slots.size(); i++)
            if(!ui_.boot_slots[i].empty())
                keys += " " + std::to_string(i + 1) + " " + ui_.boot_slots[i];
        log_.push_back("boot window: hold a white key to choose the firmware:" + keys);
        Debug("boot window");
    }

    /** The bootloader fades every LED through random colours; the same here. */
    void NewBootColour()
    {
        const uint32_t t = uint32_t(SDL_GetTicks());
        boot_r_          = float(t % 66) / 66.f;
        boot_g_          = float((t / 7) % 53) / 53.f;
        boot_b_          = float((t / 13) % 36) / 36.f;
    }

    void ServiceBoot(double dt)
    {
        if(!boot_phase_)
            return;
        boot_bright_ += boot_inc_ * float(dt);
        if(boot_bright_ > 1.f)
            boot_inc_ = -boot_inc_;
        else if(boot_bright_ < 0.f)
        {
            NewBootColour();
            boot_inc_ = -boot_inc_;
        }
        const float b   = std::clamp(boot_bright_, 0.f, 1.f);
        ui_.boot_led    = Rgb{uint8_t(boot_r_ * b * 255), uint8_t(boot_g_ * b * 255), uint8_t(boot_b_ * b * 255)};
        ui_.boot_left   = float(std::max(0.0, boot_until_ - now_s_));
        ui_.boot_choice = boot_choice_;
        // a key held from before the window opened never sends a key-down: poll the keyboard too
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        for(int i = 0; i < 4; i++)
            if(keys[kBootKeys[i]])
                ChooseBoot(i);
        if(now_s_ >= boot_until_)
            FinishBoot();
    }

    void ChooseBoot(int slot)
    {
        if(slot < 0 || slot >= int(ui_.boot_slots.size()) || ui_.boot_slots[size_t(slot)].empty())
            return;
        if(boot_choice_ != slot)
            Debug("boot choice %d (%s)", slot, BootOrder()[size_t(slot)].c_str());
        boot_choice_ = slot;
    }

    /** The window is over: boot the chosen firmware, here or in a relaunch. */
    void FinishBoot()
    {
        // no key: this firmware, the one the launcher picked (the remembered choice, or --firmware)
        const std::string choice = boot_choice_ >= 0 ? BootOrder()[size_t(boot_choice_)] : ui_.firmware;
        boot_phase_     = false;
        ui_.boot_window = false;
        SaveBootChoice(opt_.card, choice);
        if(choice != ui_.firmware)
        {
            for(int i = 0; i < int(sizeof(gui::kFirmwares) / sizeof(gui::kFirmwares[0])); i++)
                if(choice == gui::kFirmwares[i].id)
                    SwitchFirmware(i); // relaunches; returns only when that firmware is not built
            log_.push_back(choice + " is not built; booting " + ui_.firmware);
        }
        StartFirmware();
        MaybeStartTour();
    }

  private:
    bool InitSim()
    {
        Config cfg;
        cfg.realtime = true;
        cfg.card_dir = opt_.card;
        if(!Sim::Get().Init(cfg))
        {
            std::fprintf(stderr, "Sim::Init failed: card directory '%s' not found\n", opt_.card.c_str());
            return false;
        }
        SetupInput();
        return true;
    }

    /** The firmware boots only once audio, the device's clock, is running. */
    void StartFirmware()
    {
        if(sim_started_)
            return;
        Sim::Get().Start();
        sim_started_ = true;
    }

    /** The input sound from --input, and the aux jack. */
    void SetupInput()
    {
        if(!opt_.input.empty())
        {
            std::string err;
            if(Sim::Get().LoadInputFile(opt_.input, err))
                input_loaded_ = true;
            else
                std::fprintf(stderr, "%s\n", err.c_str());
        }
        // a loaded sound plugs the aux jack unless told otherwise, so the firmware takes it in stereo
        Sim::Get().SetLineIn(opt_.line_in >= 0 ? opt_.line_in != 0 : input_loaded_);
    }

    /** Opens the computer's microphone (--mic, F9, the MIC button) and feeds it to the simulated inputs. */
    void OpenMic()
    {
        if(mic_dev_ != 0)
            return;
        if(!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        {
            log_.push_back(std::string("microphone: ") + SDL_GetError());
            return;
        }
        SDL_AudioSpec want{}, have{};
        want.freq     = kAudioRate;
        want.format   = AUDIO_F32SYS;
        want.channels = 1;
        want.samples  = kAudioFrames;
        want.callback = CaptureCallback;
        mic_dev_      = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);
        if(mic_dev_ == 0)
        {
            log_.push_back(std::string("microphone not available: ") + SDL_GetError());
            std::fprintf(stderr, "microphone not available: %s\n", SDL_GetError());
            return;
        }
        SDL_PauseAudioDevice(mic_dev_, 0);
        log_.push_back("microphone open; if the meter stays flat, allow microphone access for the terminal");
        std::fprintf(stderr, "microphone open, %d Hz\n", have.freq);
    }

    void CloseMic()
    {
        if(mic_dev_ == 0)
            return;
        SDL_CloseAudioDevice(mic_dev_);
        mic_dev_ = 0;
        log_.push_back("microphone closed");
    }

    void ToggleMic()
    {
        if(mic_dev_ != 0)
            CloseMic();
        else
            OpenMic();
    }

    /** The LOAD button and F10: a file dialog, then the sound plays into the inputs. */
    void LoadSoundDialog()
    {
        std::string why, path = PickSoundFile(why);
        if(path.empty())
        {
            if(why != "cancelled")
                log_.push_back(why);
            return;
        }
        LoadSound(path);
    }

    /** Loads a sound into the inputs and plays it (drag-and-drop, the dialog). */
    void LoadSound(const std::string& path)
    {
        std::string err;
        if(!Sim::Get().LoadInputFile(path, err))
        {
            log_.push_back(err);
            return;
        }
        input_loaded_ = true;
        if(opt_.line_in < 0)
            Sim::Get().SetLineIn(true);
        Sim::Get().PlayInput(opt_.input_loop, opt_.input_gain);
        log_.push_back("input: " + Sim::Get().GetInputState().name);
    }

    /** The INPUT buttons in the bar. */
    void InputButton(int which)
    {
        switch(which)
        {
            case 0: LoadSoundDialog(); break;
            case 1:
                if(Sim::Get().GetInputState().playing)
                    Sim::Get().StopInput();
                else
                    Sim::Get().PlayInput(opt_.input_loop, opt_.input_gain);
                break;
            case 2: ToggleMic(); break;
            case 3: Sim::Get().SetLineIn(!Sim::Get().LineIn()); break;
            default: break;
        }
    }

    /** The firmware reported its selected sound (CC sound_cc_ on its MIDI output). */
    void SoundReported(int index, int channel)
    {
        sound_ch_ = channel;
        if(index == ui_.sound)
            return;
        ui_.sound        = index;
        std::string name = std::to_string(index + 1);
        for(const gui::SoundEntry& s : ui_.sounds)
            if(s.index == index)
                name += ": " + s.name;
        log_.push_back("sound " + name);
        Debug("sound reported %d", index);
    }

    /** A row of the sound menu: the choice goes to the firmware as a CC on its
     *  MIDI input, on the channel it reports on; the menu shows the result once
     *  the firmware reports it back. */
    void SelectSound(int row)
    {
        ui_.sound_menu = false;
        if(row < 0 || row >= int(ui_.sounds.size()))
            return;
        const uint8_t msg[3] = {uint8_t(0xB0 | (sound_ch_ & 0x0F)), uint8_t(sound_cc_), uint8_t(ui_.sounds[row].index & 0x7F)};
        Sim::Get().MidiIn(msg, 3);
        Debug("select sound %d", ui_.sounds[row].index);
    }

    /** Follows the firmware's MIDI output: the selected sound comes back as a CC.
     *  Both ports are drained every frame so their bytes do not pile up. */
    void PollMidiOut()
    {
        for(uint8_t b : Sim::Get().TakeMidiOut())
        {
            if(b >= 0xF8) // realtime bytes (clock, start, stop) pass between the others
                continue;
            if(b & 0x80)
            {
                midi_status_ = b;
                midi_n_      = 0;
                continue;
            }
            if(midi_n_ < 2)
                midi_data_[midi_n_++] = b;
            const int kind = midi_status_ & 0xF0;
            const int need = midi_status_ >= 0xF0 ? 0 : (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
            if(need == 0 || midi_n_ < need)
                continue;
            midi_n_ = 0; // running status: more data bytes make more messages
            if(sound_cc_ && kind == 0xB0 && midi_data_[0] == sound_cc_)
                SoundReported(midi_data_[1], midi_status_ & 0x0F);
        }
        Sim::Get().TakeUsbMidiOut();
    }

    bool InitVideo()
    {
        if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
        sdl_up_ = true;
        if(char* p = SDL_GetPrefPath("", "chompi-sim")) // the per-user folder that remembers the tour
        {
            pref_dir_ = p;
            SDL_free(p);
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        int winW = int(std::lround(gui::kPanelW * opt_.scale));
        int winH = int(std::lround(gui::kPanelH * opt_.scale));
        std::string title = "CHOMPI simulator";
        if(gui::FirmwareByName(ui_.firmware).name[0])
            title += std::string(" - ") + gui::FirmwareByName(ui_.firmware).name;
        window_  = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH,
                                    SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
        if(!window_)
        {
            std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return false;
        }
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if(!renderer_)
        {
            std::fprintf(stderr, "accelerated renderer unavailable (%s), using software\n", SDL_GetError());
            renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        }
        if(!renderer_)
        {
            std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
            return false;
        }
        SDL_RendererInfo info;
        if(SDL_GetRendererInfo(renderer_, &info) == 0)
            std::fprintf(stderr, "renderer: %s%s\n", info.name,
                         (info.flags & SDL_RENDERER_PRESENTVSYNC) ? " (vsync)" : "");

        // the key map prints what the piano keys say on this keyboard layout
        for(const PianoKeyMap& m : kPianoMap)
        {
            std::string name = SDL_GetKeyName(SDL_GetKeyFromScancode(m.key));
            bool        ascii = !name.empty();
            for(unsigned char c : name)
                if(c < 32 || c > 126)
                    ascii = false;
            if(!ascii) // the panel font is ASCII: fall back to the US name of the key
                name = SDL_GetScancodeName(m.key);
            ui_.piano_keys[size_t(m.semitone)] = name;
        }

        // Drawing happens in output pixels, mouse events arrive in window points.
        int outW = winW, outH = winH;
        SDL_GetRendererOutputSize(renderer_, &outW, &outH);
        draw_scale_  = float(outW) / gui::kPanelW;
        mouse_scale_ = float(winW) / gui::kPanelW;
        panel_       = std::make_unique<gui::Panel>(renderer_, draw_scale_);
        return true;
    }

    void InitAudio()
    {
        if(!opt_.no_audio && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
        {
            SDL_AudioSpec want{}, have{};
            want.freq     = kAudioRate;
            want.format   = AUDIO_F32SYS;
            want.channels = 2;
            want.samples  = kAudioFrames;
            want.callback = AudioCallback;
            want.userdata = &pair_;
            audio_dev_    = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
            if(audio_dev_ != 0)
            {
                audio_desc_ = std::string("audio ") + (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?")
                              + " " + std::to_string(have.freq) + " Hz/" + std::to_string(have.samples) + " pair "
                              + std::to_string(pair_) + (pair_ == 0 ? " (hp)" : " (line)");
                SDL_PauseAudioDevice(audio_dev_, 0);
                std::fprintf(stderr, "%s\n", audio_desc_.c_str());
                if(opt_.mic)
                    OpenMic();
                if(!boot_phase_)
                    StartFirmware();
                return;
            }
            std::fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        }
        Sim::Get().StartNullAudio();
        null_audio_ = true;
        audio_desc_ = "audio none (null clock)";
        std::fprintf(stderr, "running on the null audio clock\n");
        if(!boot_phase_)
            StartFirmware();
    }

    void Shutdown()
    {
        if(mic_dev_ != 0)
        {
            SDL_CloseAudioDevice(mic_dev_);
            mic_dev_ = 0;
        }
        if(audio_dev_ != 0)
        {
            SDL_CloseAudioDevice(audio_dev_);
            audio_dev_ = 0;
        }
        if(null_audio_)
        {
            Sim::Get().StopNullAudio();
            null_audio_ = false;
        }
        if(sim_started_)
        {
            Sim::Get().Stop();
            sim_started_ = false;
        }
        panel_.reset();
        if(renderer_)
            SDL_DestroyRenderer(renderer_);
        if(window_)
            SDL_DestroyWindow(window_);
        renderer_ = nullptr;
        window_   = nullptr;
        if(sdl_up_)
            SDL_Quit();
        sdl_up_ = false;
    }

    // ---- controls ----
    void Turn(int enc, int detents)
    {
        if(enc < 0 || enc >= kNumEncoders || detents == 0)
            return;
        Sim::Get().TurnEncoder(enc, detents);
        ui_.knob_angle[size_t(enc)] += detents * kDegreesPerDetent;
        if(IsSmallKnob(enc))
            last_small_knob_ = enc;
    }

    void Touch(int enc)
    {
        if(IsSmallKnob(enc))
            last_small_knob_ = enc;
    }

    /** z and x: the octave of the keybed the computer keyboard starts at. */
    void SetPianoOctave(int octave)
    {
        piano_octave_    = std::clamp(octave, 0, 1);
        ui_.piano_octave = piano_octave_;
        Debug("keyboard octave %d", piano_octave_);
    }

    // ---- the guided tour (tour.h): shown the first time a firmware runs, and from the ? in the bar ----
    std::string TourId() const { return ui_.firmware.empty() ? "stub" : ui_.firmware; }
    std::string TourSeenFile() const { return pref_dir_.empty() ? "" : pref_dir_ + "tour-seen"; }

    /** Whether this firmware's tour was finished or skipped before (one firmware id per line). */
    bool TourSeen() const
    {
        if(TourSeenFile().empty())
            return false;
        std::ifstream f(TourSeenFile());
        std::string   line;
        while(std::getline(f, line))
            if(line == TourId())
                return true;
        return false;
    }

    void MarkTourSeen()
    {
        if(TourSeenFile().empty() || TourSeen())
            return;
        std::ofstream f(TourSeenFile(), std::ios::app);
        f << TourId() << "\n";
    }

    void StartTour()
    {
        ui_.tour_step  = 0;
        ui_.sound_menu = false;
        Debug("tour start");
    }

    void EndTour()
    {
        if(ui_.tour_step < 0)
            return;
        ui_.tour_step = -1;
        MarkTourSeen();
        Debug("tour end");
    }

    /** One step on (+1) or back (-1); going past the last step ends the tour. */
    void TourMove(int d)
    {
        if(ui_.tour_step < 0)
            return;
        const int n = gui::TourFor(ui_.firmware).count;
        const int s = ui_.tour_step + d;
        if(s >= n)
            EndTour();
        else
        {
            ui_.tour_step = std::max(0, s);
            Debug("tour step %d", ui_.tour_step);
        }
    }

    gui::Hit HitAtMouse(int wx, int wy) const
    {
        return panel_->HitTest(ui_, float(wx) / mouse_scale_, float(wy) / mouse_scale_);
    }

    void MousePress(const gui::Hit& h)
    {
        MouseRelease(); // only one control at a time
        if(boot_phase_)
        {
            if(h.kind == gui::HitKind::PianoKey)
                for(int i = 0; i < 4; i++)
                    if(h.index == kBootSemitones[i])
                        ChooseBoot(i);
            return;
        }
        if(ui_.tour_step >= 0)
        {
            switch(h.kind)
            {
                case gui::HitKind::TourBack: TourMove(-1); break;
                case gui::HitKind::TourClose: EndTour(); break;
                default: TourMove(+1); break; // NEXT, or anywhere else in the window
            }
            return;
        }
        if(ui_.sound_menu && h.kind != gui::HitKind::SoundRow && h.kind != gui::HitKind::SoundButton)
        {
            ui_.sound_menu = false; // a click anywhere else just closes the menu
            return;
        }
        mouse_hit_ = h;
        switch(h.kind)
        {
            case gui::HitKind::PianoKey: input_.MouseButton(kPianoKeys[h.index], true); break;
            case gui::HitKind::FuncKey: input_.MouseButton(h.index, true); break;
            case gui::HitKind::Knob:
                // left button on a knob starts a drag-to-turn; a click without
                // movement becomes a short push on release (see MouseRelease)
                drag_enc_     = h.index;
                drag_start_y_ = mouse_y_logical_;
                drag_emitted_ = 0;
                dragged_      = false;
                Touch(h.index);
                break;
            case gui::HitKind::Toggle: Sim::Get().SetToggle(!Sim::Get().ToggleDown()); break;
            case gui::HitKind::FirmwareTab: SwitchFirmware(h.index); break;
            case gui::HitKind::InputButton: InputButton(h.index); break;
            case gui::HitKind::SoundButton: ui_.sound_menu = !ui_.sound_menu; break;
            case gui::HitKind::SoundRow: SelectSound(h.index); break;
            case gui::HitKind::HelpButton: StartTour(); break;
            case gui::HitKind::TourNext:
            case gui::HitKind::TourBack:
            case gui::HitKind::TourClose:
            case gui::HitKind::None: break;
        }
    }

    void MouseRelease()
    {
        switch(mouse_hit_.kind)
        {
            case gui::HitKind::PianoKey: input_.MouseButton(kPianoKeys[mouse_hit_.index], false); break;
            case gui::HitKind::FuncKey: input_.MouseButton(mouse_hit_.index, false); break;
            case gui::HitKind::Knob:
                if(!dragged_)
                    QuickPush(mouse_hit_.index);
                drag_enc_ = -1;
                break;
            default: break;
        }
        mouse_hit_ = gui::Hit{};
    }

    /** A click on a knob without dragging: push the encoder for a moment. */
    void QuickPush(int enc)
    {
        input_.MouseEncoder(enc, true);
        push_release_at_ = now_s_ + 0.12;
        push_enc_        = enc;
        Debug("push enc=%d", enc);
    }

    void ServiceTimedPush()
    {
        if(push_enc_ >= 0 && now_s_ >= push_release_at_)
        {
            input_.MouseEncoder(push_enc_, false);
            push_enc_ = -1;
        }
    }

    /** Input tracing, enabled with CHOMPI_SIM_GUI_DEBUG=1 in the environment. */
    void Debug(const char* fmt, ...)
    {
        if(!debug_)
            return;
        va_list va;
        va_start(va, fmt);
        std::vfprintf(stderr, fmt, va);
        std::fputc('\n', stderr);
        va_end(va);
    }

    void HandleKey(const SDL_KeyboardEvent& e)
    {
        bool        down = e.type == SDL_KEYDOWN;
        SDL_Keycode k    = e.keysym.sym;
        int         enc = 0, detents = 0;
        if(boot_phase_)
        {
            // the boot window: the first four white keys choose, nothing else reaches the instrument
            if(down && k == SDLK_ESCAPE)
                running_ = false;
            else if(down)
                for(int i = 0; i < 4; i++)
                    if(e.keysym.scancode == kBootKeys[i])
                        ChooseBoot(i);
            return;
        }
        if(ui_.tour_step >= 0
           && (k == SDLK_RIGHT || k == SDLK_LEFT || k == SDLK_PAGEDOWN || k == SDLK_PAGEUP || k == SDLK_ESCAPE))
        {
            // the tour takes these; the instrument's own keys keep working meanwhile
            if(down && !e.repeat)
            {
                if(k == SDLK_ESCAPE)
                    EndTour();
                else
                    TourMove(k == SDLK_RIGHT || k == SDLK_PAGEDOWN ? +1 : -1);
            }
            return;
        }
        if(TurnKey(k, last_small_knob_, enc, detents))
        {
            if(down) // auto-repeat is welcome here: holding the key keeps turning
                Turn(enc, detents);
            return;
        }
        if(e.repeat)
            return;
        const SDL_Scancode sc = e.keysym.scancode;
        if(!down && sc < SDL_NUM_SCANCODES && held_button_[sc] >= 0)
        {
            // let go of the cap this key pressed, whatever the octave is now
            input_.KeyboardButton(held_button_[sc], false);
            held_button_[sc] = -1;
            return;
        }
        const int span = PianoSpanSemitone(sc);
        if(span >= 0)
        {
            const int semitone = span + 12 * piano_octave_;
            if(down && semitone <= 24) // past the top cap otherwise
            {
                held_button_[sc] = kPianoKeys[semitone];
                input_.KeyboardButton(kPianoKeys[semitone], true);
            }
        }
        else if((sc == kOctaveDownKey || sc == kOctaveUpKey) && down)
            SetPianoOctave(sc == kOctaveUpKey ? 1 : 0);
        else if(k == SDLK_SPACE)
            input_.KeyboardButton(KEY_PLAY, down);
        else if(k == SDLK_RETURN || k == SDLK_KP_ENTER)
            input_.KeyboardButton(KEY_LOOP, down);
        else if(k == SDLK_LSHIFT)
            input_.KeyboardButton(KEY_CHOMPI, down);
        else if(k == SDLK_TAB && down)
            Sim::Get().SetToggle(!Sim::Get().ToggleDown());
        else if((k == SDLK_SLASH || k == SDLK_QUESTION) && down)
        {
            ui_.keymap = !ui_.keymap;
            Debug("keymap %s", ui_.keymap ? "on" : "off");
        }
        else if(k == SDLK_F7 && down)
        {
            Sim::Get().PlayInput(opt_.input_loop, opt_.input_gain);
            Debug("input play");
        }
        else if(k == SDLK_F8 && down)
        {
            Sim::Get().StopInput();
            Debug("input stop");
        }
        else if(k == SDLK_F9 && down)
            ToggleMic();
        else if(k == SDLK_F10 && down)
            LoadSoundDialog();
        else if(k >= SDLK_F1 && k <= SDLK_F6)
        {
            int e2 = int(k - SDLK_F1); // F1..F6 = ENC_SW1..ENC_SW6
            input_.KeyboardEncoder(e2, down);
            if(down)
                Touch(e2);
        }
        else if(k == SDLK_ESCAPE && down)
        {
            if(ui_.sound_menu)
                ui_.sound_menu = false;
            else
                running_ = false;
        }
    }

    void HandleEvent(const SDL_Event& e)
    {
        switch(e.type)
        {
            case SDL_DROPFILE:
            {
                std::string path = e.drop.file ? e.drop.file : "";
                SDL_free(e.drop.file);
                LoadSound(path);
                break;
            }
            case SDL_QUIT: running_ = false; break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: HandleKey(e.key); break;
            case SDL_MOUSEBUTTONDOWN:
            {
                mouse_y_logical_ = float(e.button.y) / mouse_scale_;
                gui::Hit h       = HitAtMouse(e.button.x, e.button.y);
                if(e.button.button == SDL_BUTTON_LEFT)
                    MousePress(h);
                else if(h.kind == gui::HitKind::Knob)
                {
                    // right or middle button holds the encoder's push switch down
                    held_push_enc_ = h.index;
                    input_.MouseEncoder(h.index, true);
                    Debug("push hold enc=%d", h.index);
                }
                break;
            }
            case SDL_MOUSEBUTTONUP:
                if(e.button.button == SDL_BUTTON_LEFT)
                    MouseRelease();
                else if(held_push_enc_ >= 0)
                {
                    input_.MouseEncoder(held_push_enc_, false);
                    held_push_enc_ = -1;
                }
                break;
            case SDL_MOUSEMOTION:
                mouse_y_logical_ = float(e.motion.y) / mouse_scale_;
                ui_.hover        = HitAtMouse(e.motion.x, e.motion.y);
                if(drag_enc_ >= 0 && (e.motion.state & SDL_BUTTON_LMASK))
                {
                    // dragging up turns clockwise, kDragPixelsPerDetent per detent
                    float dy    = drag_start_y_ - mouse_y_logical_;
                    int   total = int(dy / kDragPixelsPerDetent);
                    if(std::fabs(dy) > 3.f)
                        dragged_ = true;
                    if(total != drag_emitted_)
                    {
                        Turn(drag_enc_, total - drag_emitted_);
                        Debug("drag enc=%d detents=%+d", drag_enc_, total - drag_emitted_);
                        drag_emitted_ = total;
                    }
                }
                break;
            case SDL_MOUSEWHEEL:
            {
                int mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                gui::Hit h = HitAtMouse(mx, my);
                if(h.kind != gui::HitKind::Knob)
                    break;
                // Trackpads and Magic Mice deliver fractional deltas, and the integer
                // fields are often 0 for them: accumulate and emit whole detents.
                // Horizontal scrolling turns too, so two-finger swipes work.
#if SDL_VERSION_ATLEAST(2, 0, 18)
                float d = e.wheel.preciseY + e.wheel.preciseX;
#else
                float d = float(e.wheel.y + e.wheel.x);
#endif
                if(e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                    d = -d;
                float& acc = wheel_accum_[size_t(h.index)];
                acc += d;
                int det = int(acc); // whole detents only, remainder carries over
                if(det != 0)
                {
                    det = std::max(-3, std::min(3, det));
                    acc -= float(det);
                    Turn(h.index, det);
                    Debug("wheel enc=%d detents=%+d", h.index, det);
                }
                break;
            }
            case SDL_WINDOWEVENT:
                if(e.window.event == SDL_WINDOWEVENT_LEAVE)
                    ui_.hover = gui::Hit{};
                else if(e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                {
                    input_.ReleaseKeyboard(); // no key-up events arrive once focus is gone
                    held_button_.fill(-1);
                }
                break;
            default: break;
        }
    }

    void UpdateStatus()
    {
        PollMidiOut();
        for(std::string& line : Sim::Get().TakeLog())
            log_.push_back(std::move(line));
        while(log_.size() > kLogLines)
            log_.pop_front();
        ui_.log.assign(log_.begin(), log_.end());
        for(int i = 0; i < kNumEncoders; i++)
            ui_.knob_pressed[size_t(i)] = input_.EncoderPressed(i);
        ui_.arrow_knob = last_small_knob_;
        ui_.input      = Sim::Get().GetInputState();
        ui_.mic_open   = mic_dev_ != 0;
        input_level_   = std::max(Sim::Get().TakeInputPeak(), input_level_ * 0.9f);
        ui_.input_level = input_level_;

        Stats st = Sim::Get().GetStats();
        char  buf[256];
        std::snprintf(buf, sizeof buf, "%s   %s   blocks %llu   now %u ms   max block %.0f us   fw %s",
                      Sim::Get().GetConfig().realtime ? "REALTIME" : "LOCKSTEP", audio_desc_.c_str(),
                      static_cast<unsigned long long>(st.blocks_rendered), Sim::Get().NowMs(), st.max_block_us,
                      Sim::Get().FirmwareRunning() ? "running" : "stopped");
        ui_.status = buf;
        if(boot_phase_)
        {
            char bb[160];
            std::snprintf(bb, sizeof bb, "BOOT WINDOW %.1f s   hold a white key to choose the firmware; no key boots %s", ui_.boot_left,
                          ui_.boot_default.empty() ? "the last choice" : ui_.boot_default.c_str());
            ui_.status = bb;
        }
        if(piano_octave_ > 0)
            ui_.status += "   keys: octave up (z = down)";

        InputState in = Sim::Get().GetInputState();
        if(!in.name.empty() || mic_dev_ != 0)
        {
            char ib[200];
            if(!in.name.empty())
                std::snprintf(ib, sizeof ib, "   in: %s %.1f/%.1f s %s%s", in.name.c_str(), in.position_s, in.length_s,
                              in.playing ? (in.loop ? "looping" : "playing") : "stopped (F7 plays)",
                              mic_dev_ ? " + mic" : "");
            else
                std::snprintf(ib, sizeof ib, "   in: microphone");
            ui_.status += ib;
            ui_.status += in.line_in ? "   jack: aux" : "   jack: none (mic)";
        }
    }

    void Loop()
    {
        const Uint64 freq    = SDL_GetPerformanceFrequency();
        const Uint64 t0      = SDL_GetPerformanceCounter();
        const double frameS  = 1.0 / 60.0;
        bool         shotDue = !opt_.screenshot.empty();
        uint64_t     frames  = 0;
        while(running_)
        {
            frames++;
            Uint64    frameStart = SDL_GetPerformanceCounter();
            double    elapsed    = double(frameStart - t0) / double(freq);
            now_s_ = elapsed;
            SDL_Event e;
            while(SDL_PollEvent(&e))
                HandleEvent(e);
            ServiceTimedPush();
            ServiceBoot(elapsed - last_s_);
            last_s_ = elapsed;

            UpdateStatus();
            panel_->Draw(ui_);

            bool lastFrame = opt_.exit_after >= 0 && elapsed >= opt_.exit_after;
            if(shotDue && (elapsed >= opt_.screenshot_at || lastFrame))
            {
                shotDue = false;
                if(SaveScreenshot(renderer_, opt_.screenshot))
                    std::fprintf(stderr, "screenshot written to %s\n", opt_.screenshot.c_str());
                else
                    std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
            }
            SDL_RenderPresent(renderer_);
            if(lastFrame)
                running_ = false;

            // Cap at 60 fps when vsync does not (software renderer, virtual displays).
            double spent = double(SDL_GetPerformanceCounter() - frameStart) / double(freq);
            if(spent < frameS)
                SDL_Delay(Uint32((frameS - spent) * 1000.0));
        }
        double total = double(SDL_GetPerformanceCounter() - t0) / double(freq);
        std::fprintf(stderr, "%llu frames in %.2f s (%.1f fps)\n", static_cast<unsigned long long>(frames), total,
                     total > 0 ? frames / total : 0.0);
    }

    static constexpr size_t kLogLines = 3;

    Options                     opt_;
    int                         pair_ = 1;
    bool                        sim_started_ = false;
    bool                        sdl_up_      = false;
    bool                        null_audio_  = false;
    bool                        running_     = true;
    SDL_Window*                 window_      = nullptr;
    SDL_Renderer*               renderer_    = nullptr;
    SDL_AudioDeviceID           audio_dev_   = 0;
    SDL_AudioDeviceID           mic_dev_     = 0;
    std::string                 exe_dir_;
    std::string                 pref_dir_;    /**< SDL's per-user folder, with a trailing separator */
    bool                        input_loaded_ = false;
    float                       input_level_  = 0.f;
    std::string                 audio_desc_;
    float                       draw_scale_  = 1.f;
    float                       mouse_scale_ = 1.f;
    std::unique_ptr<gui::Panel> panel_;
    gui::UiState                ui_;
    Input                       input_;
    gui::Hit                    mouse_hit_;
    int                         last_small_knob_ = ENC_SW4;
    int                         piano_octave_    = 0;  /**< the keyboard's span starts at this octave */
    std::array<int, SDL_NUM_SCANCODES> held_button_ = MakeHeldButtons(); /**< cap pressed by each key, or -1 */
    float                       mouse_y_logical_ = 0.f;
    int                         drag_enc_        = -1;
    float                       drag_start_y_    = 0.f;
    int                         drag_emitted_    = 0;
    bool                        dragged_         = false;
    int                         held_push_enc_   = -1;
    float                       wheel_accum_[kNumEncoders] = {};
    double                      now_s_           = 0;
    double                      push_release_at_ = 0;
    int                         push_enc_        = -1;
    std::vector<std::string>    on_card_;             /**< firmwares the card holds (DetectFirmwares) */
    bool                        boot_phase_      = false; /**< the boot window is up; the firmware has not started */
    double                      boot_until_      = 0;
    double                      last_s_          = 0;
    int                         boot_choice_     = -1;
    float                       boot_bright_     = 0.f, boot_inc_ = 0.8f, boot_r_ = 0.f, boot_g_ = 0.f, boot_b_ = 0.f;
    int                         sound_cc_        = 0; /**< the firmware's sound select CC, 0 = none */
    int                         sound_ch_        = 0; /**< MIDI channel the firmware reports on */
    uint8_t                     midi_status_     = 0; /**< MIDI output parser: running status and data bytes */
    uint8_t                     midi_data_[2]    = {};
    int                         midi_n_          = 0;
    bool                        debug_           = std::getenv("CHOMPI_SIM_GUI_DEBUG") != nullptr;
    std::deque<std::string>     log_;
};

} // namespace

int main(int argc, char** argv)
{
    Options opt;
    int     rc = ParseArgs(argc, argv, opt);
    if(rc != 0)
        return rc == 2 ? 0 : 1;
    App app(opt);
    return app.Run();
}
