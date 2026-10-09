/** @file main.cpp
 *  @brief SDL2 desktop front-end for the CHOMPI simulator: command line,
 *  window, sound card, input mapping and the 60 fps event loop. */
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <string>
#include "chompi_sim/sim.h"
#include "panel.h"

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
};

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
                "  --help                this text\n",
                argv0);
}

/** Returns 0 to run, 1 on error, 2 when --help was printed. */
int ParseArgs(int argc, char** argv, Options& o)
{
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
struct PianoKeyMap
{
    SDL_Keycode key;
    int         semitone;
};
constexpr PianoKeyMap kPianoMap[] = {
    {SDLK_z, 0},  {SDLK_s, 1},  {SDLK_x, 2},  {SDLK_d, 3},  {SDLK_c, 4},  {SDLK_v, 5},  {SDLK_g, 6},
    {SDLK_b, 7},  {SDLK_h, 8},  {SDLK_n, 9},  {SDLK_j, 10}, {SDLK_m, 11}, {SDLK_q, 12}, {SDLK_2, 13},
    {SDLK_w, 14}, {SDLK_3, 15}, {SDLK_e, 16}, {SDLK_r, 17}, {SDLK_5, 18}, {SDLK_t, 19}, {SDLK_6, 20},
    {SDLK_y, 21}, {SDLK_7, 22}, {SDLK_u, 23}, {SDLK_i, 24},
};

int PianoSemitone(SDL_Keycode k)
{
    for(const PianoKeyMap& m : kPianoMap)
        if(m.key == k)
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
// The application
// ---------------------------------------------------------------------------
class App
{
  public:
    explicit App(const Options& o) : opt_(o), pair_(o.pair) {}
    ~App() { Shutdown(); }

    int Run()
    {
        if(!InitSim())
            return 1;
        if(!InitVideo())
            return 1;
        InitAudio();
        Loop();
        return 0;
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
        Sim::Get().Start();
        sim_started_ = true;
        return true;
    }

    bool InitVideo()
    {
        if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
        sdl_up_ = true;
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        int winW = int(std::lround(gui::kPanelW * opt_.scale));
        int winH = int(std::lround(gui::kPanelH * opt_.scale));
        window_  = SDL_CreateWindow("CHOMPI simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH,
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
                return;
            }
            std::fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        }
        Sim::Get().StartNullAudio();
        null_audio_ = true;
        audio_desc_ = "audio none (null clock)";
        std::fprintf(stderr, "running on the null audio clock\n");
    }

    void Shutdown()
    {
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

    gui::Hit HitAtMouse(int wx, int wy) const
    {
        return panel_->HitTest(float(wx) / mouse_scale_, float(wy) / mouse_scale_);
    }

    void MousePress(const gui::Hit& h)
    {
        MouseRelease(); // only one control at a time
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
        if(TurnKey(k, last_small_knob_, enc, detents))
        {
            if(down) // auto-repeat is welcome here: holding the key keeps turning
                Turn(enc, detents);
            return;
        }
        if(e.repeat)
            return;
        int semitone = PianoSemitone(k);
        if(semitone >= 0)
            input_.KeyboardButton(kPianoKeys[semitone], down);
        else if(k == SDLK_SPACE)
            input_.KeyboardButton(KEY_PLAY, down);
        else if(k == SDLK_RETURN || k == SDLK_KP_ENTER)
            input_.KeyboardButton(KEY_LOOP, down);
        else if(k == SDLK_LSHIFT)
            input_.KeyboardButton(KEY_CHOMPI, down);
        else if(k == SDLK_TAB && down)
            Sim::Get().SetToggle(!Sim::Get().ToggleDown());
        else if(k >= SDLK_F1 && k <= SDLK_F6)
        {
            int e2 = int(k - SDLK_F1); // F1..F6 = ENC_SW1..ENC_SW6
            input_.KeyboardEncoder(e2, down);
            if(down)
                Touch(e2);
        }
        else if(k == SDLK_ESCAPE && down)
            running_ = false;
    }

    void HandleEvent(const SDL_Event& e)
    {
        switch(e.type)
        {
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
                    input_.ReleaseKeyboard(); // no key-up events arrive once focus is gone
                break;
            default: break;
        }
    }

    void UpdateStatus()
    {
        for(std::string& line : Sim::Get().TakeLog())
        {
            log_.push_back(std::move(line));
            while(log_.size() > kLogLines)
                log_.pop_front();
        }
        ui_.log.assign(log_.begin(), log_.end());
        for(int i = 0; i < kNumEncoders; i++)
            ui_.knob_pressed[size_t(i)] = input_.EncoderPressed(i);
        ui_.arrow_knob = last_small_knob_;

        Stats st = Sim::Get().GetStats();
        char  buf[256];
        std::snprintf(buf, sizeof buf, "%s   %s   blocks %llu   now %u ms   max block %.0f us   fw %s",
                      Sim::Get().GetConfig().realtime ? "REALTIME" : "LOCKSTEP", audio_desc_.c_str(),
                      static_cast<unsigned long long>(st.blocks_rendered), Sim::Get().NowMs(), st.max_block_us,
                      Sim::Get().FirmwareRunning() ? "running" : "stopped");
        ui_.status = buf;
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
    std::string                 audio_desc_;
    float                       draw_scale_  = 1.f;
    float                       mouse_scale_ = 1.f;
    std::unique_ptr<gui::Panel> panel_;
    gui::UiState                ui_;
    Input                       input_;
    gui::Hit                    mouse_hit_;
    int                         last_small_knob_ = ENC_SW4;
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
