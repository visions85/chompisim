/** @file panel.cpp
 *  @brief Layout tables, sprite-based drawing helpers and the panel renderer.
 *
 *  The layout follows the CHOMPI Rev4 top board: every key socket, encoder,
 *  LED and the mode switch sits where the board file places it (millimetres,
 *  EAGLE y up), scaled onto the logical canvas. The keys are Cherry MX key
 *  caps on a 20.11 mm pitch in two rows (the "black" keys are the upper row),
 *  the four small knobs and the volume knob have their LED just above them,
 *  the big transport knob is flanked by the two small indicator LEDs, and the
 *  PLAY / LOOP / CHOMPI keys are MX keys in the top row too.
 *
 *  Discs, rings and LED halos are drawn by blitting three small white alpha
 *  sprites (generated at start-up) with a colour / alpha modulation, which
 *  gives anti-aliased edges and smooth glow on both the accelerated and the
 *  software SDL renderer. */
#include "panel.h"
#include "firmware_info.h"
#include "font.h"
#include "tour.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace gui
{
using namespace chompi_sim;

namespace
{

// ---------------------------------------------------------------------------
// Board geometry (millimetres, from hardware/hardware-pcb CC_Chompi_Rev4.brd)
// mapped onto the logical canvas (kPanelW x kPanelH at --scale 1)
// ---------------------------------------------------------------------------
constexpr float kMmPx      = 3.4f;   /**< logical pixels per millimetre */
constexpr float kBoardX    = 16.f;   /**< canvas x of board x = 0 */
constexpr float kBarH      = 26.f;   /**< the firmware bar above the instrument */
constexpr float kBoardTopY = 14.f + kBarH; /**< canvas y of the board's top edge */
constexpr float kBoardWmm  = 319.75f;
constexpr float kBoardHmm  = 99.68f;

constexpr float X(float mm)
{
    return kBoardX + mm * kMmPx;
}
constexpr float Y(float mm)
{
    return kBoardTopY + (kBoardHmm - mm) * kMmPx; // EAGLE y points up, the screen down
}

constexpr float kCapMm   = 18.2f; /**< key cap size (MX, 20.11 mm pitch) */
constexpr float kCapPx   = kCapMm * kMmPx;
constexpr float kCapRad  = 7.f;   /**< key cap corner radius */
constexpr float kKeyLipH = 4.f;   /**< visible side of an unpressed key cap */
constexpr float kKeyPressDy = 3.f;
constexpr float kPi      = 3.14159265f;

/** Piano keys by semitone: board socket position and whether it is in the
 *  upper ("black") row. KEY1..KEY15 lower row, KEY16..KEY25 upper row. */
struct PianoKeyDef
{
    float x_mm, y_mm;
    bool  upper;
};
constexpr PianoKeyDef kPianoGeom[25] = {
    {22.91f, 16.56f, false},  // KEY1   C
    {32.95f, 36.56f, true},   // KEY16  C#
    {43.02f, 16.56f, false},  // KEY2   D
    {53.07f, 36.56f, true},   // KEY17  D#
    {63.13f, 16.56f, false},  // KEY3   E
    {83.24f, 16.56f, false},  // KEY4   F
    {93.29f, 36.56f, true},   // KEY18  F#
    {103.35f, 16.56f, false}, // KEY5   G
    {113.40f, 36.56f, true},  // KEY19  G#
    {123.46f, 16.56f, false}, // KEY6   A
    {133.51f, 36.56f, true},  // KEY20  A#
    {143.57f, 16.56f, false}, // KEY7   B
    {163.68f, 16.56f, false}, // KEY8   C
    {173.73f, 36.56f, true},  // KEY21  C#
    {183.79f, 16.56f, false}, // KEY9   D
    {193.84f, 36.56f, true},  // KEY22  D#
    {203.91f, 16.56f, false}, // KEY10  E
    {224.02f, 16.56f, false}, // KEY11  F
    {234.07f, 36.56f, true},  // KEY23  F#
    {244.13f, 16.56f, false}, // KEY12  G
    {254.18f, 36.56f, true},  // KEY24  G#
    {264.24f, 16.56f, false}, // KEY13  A
    {274.29f, 36.56f, true},  // KEY25  A#
    {284.35f, 16.56f, false}, // KEY14  B
    {304.46f, 16.56f, false}, // KEY15  C
};

struct KnobDef
{
    int         enc;
    const char* label;
    float       x_mm, y_mm, r_mm;
    bool        big;
    const char* push_key;         /**< computer key that pushes the encoder */
    const char* ccw_key, *cw_key; /**< keys that turn it; nullptr = the arrow keys, when touched last */
};
constexpr KnobDef kKnobs[] = {
    {ENC_SW4, "PITCH", 69.39f, 68.46f, 8.5f, false, "F4", nullptr, nullptr},
    {ENC_SW1, "A", 102.90f, 68.46f, 8.5f, false, "F1", nullptr, nullptr},
    {ENC_SW2, "B", 136.42f, 68.46f, 8.5f, false, "F2", nullptr, nullptr},
    {ENC_SW3, "C", 169.94f, 68.46f, 8.5f, false, "F3", nullptr, nullptr},
    {ENC_SW5, "TRANSPORT", 210.09f, 68.85f, 15.5f, true, "F5", "[", "]"}, // SW5 is on the lower board, under this spot
    {ENC_SW6, "VOLUME", 300.65f, 68.46f, 8.5f, false, "F6", "-", "="},
};

struct FuncKeyDef
{
    int         button;
    const char* label;
    float       x_mm, y_mm;
    const char* key; /**< computer key */
};
constexpr FuncKeyDef kFuncKeys[] = {
    {KEY_CHOMPI, "CHOMPI", 43.03f, 65.92f, "L-SHIFT"}, // KEY26
    {KEY_PLAY, "PLAY", 244.14f, 65.92f, "SPACE"},      // KEY27
    {KEY_LOOP, "LOOP", 264.26f, 65.92f, "RETURN"},     // KEY28
};

/** The ten panel LEDs, by PanelLed index (LED1..LED10 on the board). */
struct PanelLedDef
{
    float x_mm, y_mm, r_mm;
};
constexpr PanelLedDef kPanelLeds[kNumPanelLeds] = {
    {39.22f, 89.55f, 4.f},   // LED1  above the CHOMPI key
    {69.38f, 89.55f, 4.f},   // LED2  above PITCH
    {102.90f, 89.55f, 4.f},  // LED3  above A
    {136.42f, 89.55f, 4.f},  // LED4  above B
    {169.94f, 89.55f, 4.f},  // LED5  above C
    {200.11f, 91.86f, 2.5f}, // LED6  left of the transport knob
    {220.22f, 91.86f, 2.5f}, // LED7  right of the transport knob
    {240.33f, 91.86f, 2.5f}, // LED8  above PLAY
    {260.44f, 91.86f, 2.5f}, // LED9  above LOOP
    {300.65f, 89.55f, 4.f},  // LED10 above VOLUME
};

/** Mode switch SW_NORMAL: a vertical slide at the far left of the top row. */
struct ToggleDef
{
    float x_mm, y_mm;
    float w, h, thumb_w, thumb_h; /**< logical pixels */
};
constexpr ToggleDef kToggle = {18.38f, 68.46f, 16.f, 46.f, 24.f, 18.f};

constexpr SDL_FRect kBody = {6, 2 + kBarH, 1108, 348}; /**< the instrument enclosure */

/** Text rows (logical y of the top of the text). */
constexpr float kHintY1  = 356 + kBarH;
constexpr float kHintY2  = 366 + kBarH;
constexpr float kStatusY = 380 + kBarH;
constexpr float kLogY    = 392 + kBarH;
constexpr float kLogDy   = 10;
constexpr float kTextX   = 16;

const char* const kHint1 = "/ key map   ? in the bar = tour   PIANO a s d f g h j k l ; ' = white   w e t y u o p = black   "
                           "z x = octave   SPACE play   RETURN loop   L-SHIFT chompi (hold)   TAB mode   ESC quit";
const char* const kHint2 = "KNOBS  drag or scroll = turn   click = push   right-click = hold   [ ] transport   - = volume   "
                           "LEFT/RIGHT last small knob   F1-F6 push   F7/F8 input play/stop  F9 mic  F10 load";
/** Hint lines while the key map overlay is up. */
const char* const kMapHint1 = "KEY MAP   the letters on the caps play the notes   z / x move them down / up an octave   "
                              "LEFT/RIGHT arrows turn the small knob touched last   / or ? hides this map";
const char* const kMapHint2 = "MOUSE   click or hold any key   drag up/down or scroll on a knob = turn   click a knob = push   "
                              "right-click = hold   F7/F8 input play/stop  F9 mic  F10 load a sound   ESC quit";

// ---------------------------------------------------------------------------
// Colours: a cream enclosure with white caps, like the instrument. No artwork
// or logos are reproduced.
// ---------------------------------------------------------------------------
constexpr SDL_Color kBg          = {22, 22, 26, 255};
constexpr SDL_Color kBodyColor   = {232, 226, 212, 255};
constexpr SDL_Color kBodyEdge    = {196, 188, 170, 255};
constexpr SDL_Color kLowerFace   = {226, 224, 217, 255};
constexpr SDL_Color kLowerSide   = {176, 172, 162, 255};
constexpr SDL_Color kUpperFace   = {196, 193, 185, 255};
constexpr SDL_Color kUpperSide   = {146, 142, 132, 255};
constexpr SDL_Color kFuncFace    = {238, 232, 218, 255};
constexpr SDL_Color kFuncSide    = {180, 172, 152, 255};
constexpr SDL_Color kFuncText    = {70, 66, 60, 255};
constexpr SDL_Color kKnobRim     = {40, 40, 46, 255};
constexpr SDL_Color kKnobRimHot  = {90, 92, 104, 255};
constexpr SDL_Color kKnobBody    = {58, 59, 66, 255};
constexpr SDL_Color kKnobBodyDn  = {40, 41, 46, 255};
constexpr SDL_Color kBigRim      = {74, 50, 128, 255};
constexpr SDL_Color kBigRimHot   = {118, 92, 180, 255};
constexpr SDL_Color kBigBody     = {112, 80, 176, 255};
constexpr SDL_Color kBigBodyDn   = {86, 60, 140, 255};
constexpr SDL_Color kPointer     = {240, 240, 244, 255};
constexpr SDL_Color kLedLens     = {214, 208, 196, 255};
constexpr SDL_Color kLedLensEdge = {150, 144, 132, 255};
constexpr SDL_Color kLabel       = {96, 92, 84, 255};
constexpr SDL_Color kToggleSlot  = {58, 56, 52, 255};
constexpr SDL_Color kToggleThumb = {236, 234, 228, 255};
constexpr SDL_Color kToggleGrip  = {150, 146, 138, 255};
constexpr SDL_Color kHintColor   = {104, 110, 124, 255};
constexpr SDL_Color kCue         = {118, 112, 100, 255}; /**< firmware function under a control */
constexpr SDL_Color kCueDim      = {150, 146, 136, 255}; /**< its click pages */
constexpr SDL_Color kMenuCue     = {70, 66, 60, 255};    /**< menu-layer function printed on a cap */
constexpr SDL_Color kBarBg       = {18, 18, 22, 255};
constexpr SDL_Color kBarText     = {200, 204, 214, 255};
constexpr SDL_Color kTabText     = {214, 218, 228, 255};
constexpr SDL_Color kTabEdge     = {92, 96, 110, 255};
constexpr SDL_Color kTabDim      = {84, 88, 100, 255};
constexpr SDL_Color kTabOnText   = {18, 18, 22, 255};
constexpr SDL_Color kStatusColor = {170, 176, 188, 255};
constexpr SDL_Color kLogColor    = {120, 170, 226, 255};

/** Key map overlay: dark boxes with light names and amber keys and arrows. */
constexpr SDL_Color kMapBox      = {30, 32, 40, 242};
constexpr SDL_Color kMapEdge     = {118, 122, 138, 255};
constexpr SDL_Color kMapText     = {236, 236, 230, 255};
constexpr SDL_Color kMapKey      = {255, 212, 118, 255};
constexpr SDL_Color kMapArrow    = {255, 186, 76, 255};
constexpr uint8_t   kMapDim      = 64;   /**< alpha of the shade over the instrument */
constexpr float     kChipH       = 18.f; /**< height of a computer-key chip on a cap */
constexpr float     kLabelH      = 7.f;  /**< height of the 1 px label font */
constexpr float     kMapPad      = 4.f;  /**< padding inside a callout box */
constexpr float     kMapLineGap  = 3.f;  /**< gap between the two lines of a callout */

SDL_Color Scaled(SDL_Color c, float f)
{
    auto s = [f](uint8_t v) { return uint8_t(std::clamp(v * f, 0.f, 255.f)); };
    return SDL_Color{s(c.r), s(c.g), s(c.b), c.a};
}
float Brightness(Rgb c)
{
    return std::max({c.r, c.g, c.b}) / 255.f;
}
/** Key cap face lit from below by its LED: the cap takes the LED's hue. */
SDL_Color Tint(SDL_Color base, Rgb led, float amount)
{
    float b = Brightness(led);
    if(b <= 0.f)
        return base;
    float t = std::clamp(amount * b, 0.f, 1.f);
    auto  mix = [t](uint8_t a, float l) { return uint8_t(std::clamp(a * (1.f - t) + l * t, 0.f, 255.f)); };
    return SDL_Color{mix(base.r, led.r / b), mix(base.g, led.g / b), mix(base.b, led.b / b), 255};
}

} // namespace

// ---------------------------------------------------------------------------
// Canvas: logical-coordinate drawing on top of SDL_Renderer
// ---------------------------------------------------------------------------
class Canvas
{
  public:
    Canvas(SDL_Renderer* r, float scale) : r_(r), scale_(scale)
    {
        disc_ = MakeSprite([](float d) { return std::clamp(kDiscR + 0.5f - d, 0.f, 1.f); });
        ring_ = MakeSprite([](float d) {
            return std::clamp(kDiscR + 0.5f - d, 0.f, 1.f) * std::clamp(d - kRingInner + 0.5f, 0.f, 1.f);
        });
        glow_ = MakeSprite([](float d) {
            float t = d / (kSprite / 2.f);
            return t >= 1.f ? 0.f : std::pow(1.f - t, 2.4f);
        });
    }
    ~Canvas()
    {
        for(SDL_Texture* t : {disc_, ring_, glow_})
            if(t)
                SDL_DestroyTexture(t);
    }

    void Clear(SDL_Color c)
    {
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, 255);
        SDL_RenderClear(r_);
    }

    void FillRect(float x, float y, float w, float h, SDL_Color c)
    {
        SDL_FRect d = {x * scale_, y * scale_, w * scale_, h * scale_};
        SDL_SetRenderDrawBlendMode(r_, c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
        SDL_RenderFillRectF(r_, &d);
    }

    void RoundRect(float x, float y, float w, float h, float rad, SDL_Color c)
    {
        rad = std::min(rad, std::min(w, h) / 2.f);
        FillRect(x + rad, y, w - 2 * rad, h, c);
        FillRect(x, y + rad, rad, h - 2 * rad, c);
        FillRect(x + w - rad, y + rad, rad, h - 2 * rad, c);
        Disc(x + rad, y + rad, rad, c);
        Disc(x + w - rad, y + rad, rad, c);
        Disc(x + rad, y + h - rad, rad, c);
        Disc(x + w - rad, y + h - rad, rad, c);
    }

    void Disc(float cx, float cy, float r, SDL_Color c)
    {
        Sprite(disc_, cx, cy, r * scale_ * (kSprite / 2.f) / kDiscR, c, SDL_BLENDMODE_BLEND);
    }

    void Ring(float cx, float cy, float r, SDL_Color c)
    {
        Sprite(ring_, cx, cy, r * scale_ * (kSprite / 2.f) / kDiscR, c, SDL_BLENDMODE_BLEND);
    }

    /** LED halo of radius r. Additive looks right on dark surfaces; on light
     *  surfaces an alpha-blended tint of the (normalised) colour reads better. */
    void Glow(float cx, float cy, float r, Rgb c, float strength, bool additive)
    {
        float b = Brightness(c);
        if(b <= 0.f || strength <= 0.f)
            return;
        SDL_Color m;
        if(additive)
            m = SDL_Color{c.r, c.g, c.b, uint8_t(std::clamp(255.f * strength, 0.f, 255.f))};
        else
            m = SDL_Color{uint8_t(c.r / b), uint8_t(c.g / b), uint8_t(c.b / b),
                          uint8_t(std::clamp(255.f * strength * b, 0.f, 255.f))};
        Sprite(glow_, cx, cy, r * scale_, m, additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    }

    void Text(float x, float y, int px, SDL_Color c, const std::string& s, int align = -1)
    {
        int dpx = std::max(1, int(std::floor(px * scale_)));
        int w   = TextWidth(s, dpx);
        int dx  = int(std::lround(x * scale_)) - (align == 0 ? w / 2 : align > 0 ? w : 0);
        DrawText(r_, dx, int(std::lround(y * scale_)), dpx, c, s);
    }

    /** A line of width w, drawn as a run of discs so it is anti-aliased. */
    void Line(float x1, float y1, float x2, float y2, float w, SDL_Color c)
    {
        float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
        int   n = std::max(1, int(std::ceil(len / 0.75f)));
        for(int i = 0; i <= n; i++)
        {
            float t = float(i) / float(n);
            Disc(x1 + dx * t, y1 + dy * t, w / 2, c);
        }
    }

    /** An arrow from a dot at (x1, y1) to a tip at (x2, y2). */
    void Arrow(float x1, float y1, float x2, float y2, SDL_Color c)
    {
        float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
        if(len < 1.f)
            return;
        float           ux = dx / len, uy = dy / len;
        constexpr float h = 8.f, s = 0.5f; // head length and half-width
        Line(x1, y1, x2 - ux * 3.f, y2 - uy * 3.f, 2.f, c);
        Line(x2, y2, x2 - ux * h - uy * h * s, y2 - uy * h + ux * h * s, 2.f, c);
        Line(x2, y2, x2 - ux * h + uy * h * s, y2 - uy * h - ux * h * s, 2.f, c);
        Disc(x1, y1, 2.5f, c);
    }

  private:
    static constexpr int   kSprite    = 128;
    static constexpr float kDiscR     = 60.f;
    static constexpr float kRingInner = 48.f;

    template <class F>
    SDL_Texture* MakeSprite(F alphaOfDist)
    {
        std::vector<uint8_t> px(size_t(kSprite) * kSprite * 4);
        const float          half = kSprite / 2.f;
        for(int y = 0; y < kSprite; y++)
            for(int x = 0; x < kSprite; x++)
            {
                float    dx = x + 0.5f - half, dy = y + 0.5f - half;
                float    a  = std::clamp(alphaOfDist(std::sqrt(dx * dx + dy * dy)), 0.f, 1.f);
                uint8_t* p  = &px[(size_t(y) * kSprite + x) * 4];
                p[0] = p[1] = p[2] = 255;
                p[3]               = uint8_t(std::lround(a * 255.f));
            }
        SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kSprite, kSprite);
        if(!t)
            return nullptr;
        SDL_UpdateTexture(t, nullptr, px.data(), kSprite * 4);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        return t;
    }

    void Sprite(SDL_Texture* t, float cx, float cy, float halfDev, SDL_Color c, SDL_BlendMode mode)
    {
        if(!t)
            return;
        SDL_FRect d = {cx * scale_ - halfDev, cy * scale_ - halfDev, 2 * halfDev, 2 * halfDev};
        SDL_SetTextureColorMod(t, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(t, c.a);
        SDL_SetTextureBlendMode(t, mode);
        SDL_RenderCopyF(r_, t, nullptr, &d);
    }

    SDL_Renderer* r_;
    float         scale_;
    SDL_Texture*  disc_ = nullptr;
    SDL_Texture*  ring_ = nullptr;
    SDL_Texture*  glow_ = nullptr;
};

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------
namespace
{

SDL_FRect CapRect(float x_mm, float y_mm)
{
    return SDL_FRect{X(x_mm) - kCapPx / 2, Y(y_mm) - kCapPx / 2, kCapPx, kCapPx};
}
SDL_FRect PianoKeyRect(int semitone)
{
    return CapRect(kPianoGeom[semitone].x_mm, kPianoGeom[semitone].y_mm);
}
SDL_FRect FuncKeyRect(const FuncKeyDef& f)
{
    return CapRect(f.x_mm, f.y_mm);
}
bool Contains(const SDL_FRect& r, float x, float y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
SDL_FRect ToggleHitRect()
{
    const ToggleDef& t = kToggle;
    return SDL_FRect{X(t.x_mm) - t.thumb_w / 2 - 4, Y(t.y_mm) - t.h / 2 - 4, t.thumb_w + 8, t.h + 8};
}

// ---------------------------------------------------------------------------
// Drawing of the individual controls
// ---------------------------------------------------------------------------

/** A key cap: a rounded top face on a darker side; pressed caps sink and lose the side. */
void DrawKeyCap(Canvas& cv, const SDL_FRect& r, SDL_Color face, SDL_Color side, bool pressed, bool hover)
{
    float lip = pressed ? 1.f : kKeyLipH;
    float dy  = pressed ? kKeyPressDy : 0.f;
    cv.RoundRect(r.x, r.y + dy, r.w, r.h - dy, kCapRad, side);
    SDL_Color f = pressed ? Scaled(face, 0.92f) : hover ? Scaled(face, 1.04f) : face;
    cv.RoundRect(r.x, r.y + dy, r.w, r.h - lip - dy, kCapRad, f);
}

/** A through-hole LED: a pale lens that takes the LED colour, with a halo on the panel. */
void DrawLed(Canvas& cv, float cx, float cy, float r, Rgb c)
{
    cv.Disc(cx, cy, r + 1.f, kLedLensEdge);
    if(Brightness(c) <= 0.f)
    {
        cv.Disc(cx, cy, r, kLedLens);
        return;
    }
    cv.Glow(cx, cy, r * 3.2f, c, 0.85f, false);
    cv.Disc(cx, cy, r, SDL_Color{c.r, c.g, c.b, 255});
    cv.Disc(cx - r * 0.3f, cy - r * 0.3f, r * 0.3f, SDL_Color{255, 255, 255, 110});
}

/** One piano key, lit by its LED and, with the mode switch down, carrying its menu-layer name. */
void DrawPianoKey(Canvas& cv, int semi, const UiState& st)
{
    const PianoKeyDef& g       = kPianoGeom[semi];
    SDL_FRect          r       = PianoKeyRect(semi);
    bool               pressed = Sim::Get().ButtonPressed(kPianoKeys[semi]);
    bool               hover   = st.hover == Hit{HitKind::PianoKey, semi};
    Rgb                led     = Sim::Get().KeyLed(kPianoKeyLed[semi]);
    SDL_Color          face    = Tint(g.upper ? kUpperFace : kLowerFace, led, 0.75f);
    float              dy      = pressed ? kKeyPressDy : 0.f;
    float              b       = Brightness(led);
    if(b > 0.f)
        cv.Glow(r.x + r.w / 2, r.y + r.h / 2 + dy, r.w * 1.25f, led, 0.55f, true);
    DrawKeyCap(cv, r, face, g.upper ? kUpperSide : kLowerSide, pressed, hover);
    if(b > 0.f)
    {
        // the cap is lit from below: a bright window of the LED colour, strongest in the middle
        SDL_Color lit = {uint8_t(led.r / b), uint8_t(led.g / b), uint8_t(led.b / b), uint8_t(120 + 135 * b)};
        float     in  = 7.f;
        cv.RoundRect(r.x + in, r.y + in + dy, r.w - 2 * in, r.h - 2 * in - kKeyLipH + (pressed ? 3.f : 0.f), kCapRad - 2, lit);
        cv.Glow(r.x + r.w / 2, r.y + r.h / 2 + dy, r.w * 0.55f, led, 0.5f, true);
    }
    // the menu layer (mode switch down, CHOMPI held): what the key does in it
    if(Sim::Get().ToggleDown())
    {
        const FirmwareInfo& fw = FirmwareByName(st.firmware);
        int                 black = 0, white = 0;
        for(int s = 0; s <= semi; s++)
            (kPianoGeom[s].upper ? black : white)++;
        std::string cue;
        if(g.upper)
            cue = fw.menu_black[black - 1] ? fw.menu_black[black - 1] : "";
        else if(white == 15)
            cue = fw.menu_white15 ? fw.menu_white15 : "";
        else if(fw.menu_white)
            cue = std::string(fw.menu_white) + " " + std::to_string(white);
        if(!cue.empty())
            cv.Text(r.x + r.w / 2, r.y + 8 + dy, 1, kMenuCue, cue, 0);
    }
}

void DrawPianoKeys(Canvas& cv, const UiState& st)
{
    for(int semi = 0; semi < 25; semi++)
        DrawPianoKey(cv, semi, st);
}

void DrawKnob(Canvas& cv, const KnobDef& k, const UiState& st)
{
    float cx = X(k.x_mm), cy = Y(k.y_mm), R = k.r_mm * kMmPx;
    bool  pressed = st.knob_pressed[size_t(k.enc)];
    bool  hover   = st.hover == Hit{HitKind::Knob, k.enc};

    // shadow, rim, cap; a pushed knob looks smaller and darker
    float r = pressed ? R * 0.95f : R;
    cv.Disc(cx + 1.5f, cy + 2.f, r + 1.f, SDL_Color{0, 0, 0, 60});
    cv.Disc(cx, cy, r, hover ? (k.big ? kBigRimHot : kKnobRimHot) : (k.big ? kBigRim : kKnobRim));
    cv.Disc(cx, cy, r - 3, pressed ? (k.big ? kBigBodyDn : kKnobBodyDn) : (k.big ? kBigBody : kKnobBody));

    // position mark: a thick radial line made of overlapping discs
    float a  = st.knob_angle[size_t(k.enc)] * kPi / 180.f;
    float sx = std::sin(a), sy = -std::cos(a);
    float pw = std::max(1.6f, r * 0.07f);
    for(float t = 0.38f; t <= 0.86f; t += 0.04f)
        cv.Disc(cx + sx * t * r, cy + sy * t * r, pw, kPointer);

    // the firmware's function for the knob, and what its click pages hold
    const FirmwareInfo& fw    = FirmwareByName(st.firmware);
    const char* const*  pages = fw.knob[size_t(k.enc)];
    cv.Text(cx, cy + R + 6, 1, pages[0] ? kCue : kLabel, pages[0] ? pages[0] : k.label, 0);
    if(pages[1])
    {
        std::string more = std::string("push: ") + pages[1];
        if(pages[2])
            more += std::string(" / ") + pages[2];
        cv.Text(cx, cy + R + 6 + kLabelH + 3, 1, kCueDim, more, 0);
    }
}

void DrawFuncKey(Canvas& cv, const FuncKeyDef& f, const UiState& st)
{
    SDL_FRect r       = FuncKeyRect(f);
    bool      pressed = Sim::Get().ButtonPressed(f.button);
    bool      hover   = st.hover == Hit{HitKind::FuncKey, f.button};
    float     dy      = pressed ? kKeyPressDy : 0.f;
    DrawKeyCap(cv, r, kFuncFace, kFuncSide, pressed, hover);
    cv.Text(r.x + r.w / 2, r.y + r.h / 2 - 5 + dy, 1, kFuncText, f.label, 0);
    const FirmwareInfo& fw  = FirmwareByName(st.firmware);
    const bool          menu = Sim::Get().ToggleDown();
    const char* cue = f.button == KEY_CHOMPI ? (menu ? "MENU (hold)" : fw.chompi) : f.button == KEY_PLAY ? fw.play : fw.loop;
    if(cue)
        cv.Text(r.x + r.w / 2, r.y + r.h + 4, 1, kCue, cue, 0);
}

void DrawToggle(Canvas& cv, const UiState& st)
{
    const ToggleDef& t     = kToggle;
    float            cx    = X(t.x_mm), cy = Y(t.y_mm);
    bool             down  = Sim::Get().ToggleDown();
    bool             hover = st.hover == Hit{HitKind::Toggle, 0};
    cv.RoundRect(cx - t.w / 2, cy - t.h / 2, t.w, t.h, t.w / 2, kToggleSlot);
    float ty = down ? cy + t.h / 2 - t.thumb_h - 1 : cy - t.h / 2 + 1;
    cv.RoundRect(cx - t.thumb_w / 2, ty, t.thumb_w, t.thumb_h, 5, hover ? Scaled(kToggleThumb, 1.04f) : kToggleThumb);
    cv.FillRect(cx - t.thumb_w / 2 + 6, ty + t.thumb_h / 2 - 1, t.thumb_w - 12, 2, kToggleGrip);
    cv.Text(cx, cy + t.h / 2 + 8, 1, kLabel, down ? "MENU" : "MODE", 0);
}

void DrawTextArea(Canvas& cv, const UiState& st)
{
    cv.Text(kTextX, kHintY1, 1, kHintColor, st.keymap ? kMapHint1 : kHint1);
    cv.Text(kTextX, kHintY2, 1, kHintColor, st.keymap ? kMapHint2 : kHint2);
    cv.Text(kTextX, kStatusY, 1, kStatusColor, st.status);
    float y = kLogY;
    for(const std::string& line : st.log)
    {
        cv.Text(kTextX, y, 1, kLogColor, "> " + line);
        y += kLogDy;
    }
}

// ---------------------------------------------------------------------------
// Key map overlay: the computer keys, drawn over a shaded instrument. The
// piano caps carry their letters; every other control gets a description box
// with an arrow to it.
// ---------------------------------------------------------------------------

/** A key-shaped chip with a computer key's name, centred on (cx, cy). A
 *  single character is drawn large, a key name small. */
void DrawKeyChip(Canvas& cv, float cx, float cy, const char* name)
{
    if(!name || !*name)
        return;
    const std::string s(name);
    const int         px = s.size() == 1 ? 2 : 1;
    const float       tw = float(TextWidth(s, px)), th = float(TextHeight(px));
    const float       w  = std::max(kChipH, tw + 10.f);
    cv.RoundRect(cx - w / 2 - 1, cy - kChipH / 2 - 1, w + 2, kChipH + 2, 5.f, kMapEdge);
    cv.RoundRect(cx - w / 2, cy - kChipH / 2, w, kChipH, 4.f, kMapBox);
    cv.Text(cx, cy - th / 2, px, kMapKey, s, 0);
}

/** A description box (name, keys) with an arrow from one of its edges to a
 *  point on the control. */
struct Callout
{
    std::string name;
    std::string keys;
    float       cx;    /**< box centre x */
    float       top;   /**< box top y */
    float       ax;    /**< x where the arrow leaves the box */
    bool        below; /**< the arrow leaves the bottom edge (else the top edge) */
    float       tx, ty; /**< arrow tip */
};

void DrawCallout(Canvas& cv, const Callout& c)
{
    const float w = float(std::max(TextWidth(c.name, 1), TextWidth(c.keys, 1))) + 2 * kMapPad + 2;
    const float h = 2 * kLabelH + kMapLineGap + 2 * kMapPad;
    const float x = c.cx - w / 2;
    cv.RoundRect(x - 1, c.top - 1, w + 2, h + 2, 5.f, kMapEdge);
    cv.RoundRect(x, c.top, w, h, 4.f, kMapBox);
    cv.Text(c.cx, c.top + kMapPad, 1, kMapText, c.name, 0);
    cv.Text(c.cx, c.top + kMapPad + kLabelH + kMapLineGap, 1, kMapKey, c.keys, 0);
    cv.Arrow(c.ax, c.below ? c.top + h + 1 : c.top - 1, c.tx, c.ty, kMapArrow);
}

void DrawKeyMap(Canvas& cv, const UiState& st)
{
    cv.FillRect(kBody.x, kBody.y, kBody.w, kBody.h, SDL_Color{0, 0, 0, kMapDim});

    // the computer key on each cap, for the octave the keyboard is at
    for(int semi = 0; semi < 25; semi++)
    {
        const int s = semi - 12 * st.piano_octave;
        if(s < 0 || s >= kPianoSpan || st.piano_keys[size_t(s)].empty())
            continue;
        SDL_FRect r  = PianoKeyRect(semi);
        float     dy = Sim::Get().ButtonPressed(kPianoKeys[semi]) ? kKeyPressDy : 0.f;
        DrawKeyChip(cv, r.x + r.w / 2, r.y + r.h - kKeyLipH - kChipH / 2 - 3 + dy, st.piano_keys[size_t(s)].c_str());
    }

    // Every box sits in the free band between the firmware bar and the LED
    // row; an arrow that would cross the LED above a knob leaves the box off
    // centre and lands on the knob's shoulder.
    constexpr float kBandTop = kBarH + 5.f;
    std::vector<Callout> callouts;
    {
        const ToggleDef& t = kToggle;
        callouts.push_back({"MODE switch", "TAB (latches)", 60.f, kBandTop, 72.f, true, X(t.x_mm), Y(t.y_mm) - t.h / 2 - 3});
    }
    for(const FuncKeyDef& f : kFuncKeys)
    {
        SDL_FRect r  = FuncKeyRect(f);
        float     cx = r.x + r.w / 2;
        float     ax = cx + (f.button == KEY_CHOMPI ? 16.f : 6.f);
        callouts.push_back({f.label, f.key, cx + (f.button == KEY_CHOMPI ? -2.f : 0.f), kBandTop, ax, true, ax, r.y - 2});
    }
    for(const KnobDef& k : kKnobs)
    {
        float       cx = X(k.x_mm), cy = Y(k.y_mm), R = k.r_mm * kMmPx;
        std::string keys = k.ccw_key ? std::string(k.ccw_key) + " " + k.cw_key + " turn  " + k.push_key + " push"
                                     : std::string(k.push_key) + " push";
        if(!k.ccw_key && st.arrow_knob == k.enc)
            keys += " < > turn";
        if(k.big)
            callouts.push_back({k.label, keys, cx, kBandTop, cx, true, cx, cy - R - 2});
        else
            callouts.push_back({k.label, keys, cx + (k.enc == ENC_SW4 ? 4.f : 0.f), kBandTop, cx + 28.f, true, cx + R * 0.5f + 1, cy - R * 0.866f - 1});
    }
    for(const Callout& c : callouts)
        DrawCallout(cv, c);
}

// ---------------------------------------------------------------------------
// Firmware bar: which firmware runs, tabs to switch, the card folder
// ---------------------------------------------------------------------------
constexpr float kTabX = 86.f, kTabW = 48.f, kTabH = 18.f, kTabGap = 5.f, kTabY = (kBarH - kTabH) / 2;

SDL_FRect TabRect(int i)
{
    return SDL_FRect{kTabX + i * (kTabW + kTabGap), kTabY, kTabW, kTabH};
}

/** The INPUT section of the bar: four buttons, a meter and the sound's name. */
constexpr float kInputX = 560.f, kMeterX = 812.f, kMeterW = 60.f;
constexpr float kInputButtonX[4] = {598.f, 648.f, 698.f, 742.f};
constexpr float kInputButtonW[4] = {44.f, 44.f, 38.f, 62.f};

SDL_FRect InputButtonRect(int i)
{
    return SDL_FRect{kInputButtonX[i], kTabY, kInputButtonW[i], kTabH};
}

/** The SOUND menu of a firmware with a sound list (GRAIN): a button in the
 *  bar after the firmware's name, and a list dropped over the instrument. */
constexpr float kSoundLabelX = 372.f, kSoundX = 410.f, kSoundW = 142.f;
constexpr float kSoundRowH = 14.f, kSoundListW = 236.f, kSoundListPad = 3.f;

bool HasSoundMenu(const UiState& st)
{
    return FirmwareByName(st.firmware).sound_cc != 0;
}

/** The ? at the right end of the bar: the guided tour. */
constexpr float kHelpW = 20.f;
SDL_FRect       HelpButtonRect()
{
    return SDL_FRect{float(kPanelW) - 16 - kHelpW, kTabY, kHelpW, kTabH};
}
SDL_FRect SoundButtonRect()
{
    return SDL_FRect{kSoundX, kTabY, kSoundW, kTabH};
}
SDL_FRect SoundRowRect(int i)
{
    return SDL_FRect{kSoundX, kBarH + kSoundListPad + i * kSoundRowH, kSoundListW, kSoundRowH};
}

/** Cuts a text to a width, with "..". */
std::string Fit(std::string t, float w)
{
    if(float(TextWidth(t, 1)) <= w)
        return t;
    while(t.size() > 1 && float(TextWidth(t + "..", 1)) > w)
        t.pop_back();
    return t + "..";
}

/** "3  03_cubbi_a3": the sound's number and its name. */
std::string SoundLabel(const SoundEntry& s, float w)
{
    return Fit(std::to_string(s.index + 1) + "  " + s.name, w);
}

/** A small chevron pointing down (or up) centred on (cx, cy). */
void DrawChevron(Canvas& cv, float cx, float cy, bool up, SDL_Color c)
{
    const float d = up ? -2.f : 2.f;
    cv.Line(cx - 4, cy - d, cx, cy + d, 1.6f, c);
    cv.Line(cx, cy + d, cx + 4, cy - d, 1.6f, c);
}

void DrawSoundButton(Canvas& cv, const UiState& st, SDL_Color accent)
{
    cv.Text(kSoundLabelX, kTabY + 5, 1, kHintColor, "SOUND");
    const SDL_FRect r     = SoundButtonRect();
    const bool      hover = st.hover == Hit{HitKind::SoundButton, 0};
    cv.RoundRect(r.x, r.y, r.w, r.h, 4.f, st.sound_menu || hover ? accent : kTabEdge);
    cv.RoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 3.f, kBarBg);
    std::string label = st.sound < 0 ? "loading" : std::to_string(st.sound + 1);
    for(const SoundEntry& s : st.sounds)
        if(s.index == st.sound)
            label = SoundLabel(s, r.w - 24);
    cv.Text(r.x + 6, r.y + 5, 1, st.sound < 0 ? kTabDim : kTabText, label);
    DrawChevron(cv, r.x + r.w - 9, r.y + r.h / 2, st.sound_menu, kTabText);
}

/** The dropped-down list: the selected sound in the firmware's colour, the hovered row lighter. */
void DrawSoundMenu(Canvas& cv, const UiState& st)
{
    if(!st.sound_menu || !HasSoundMenu(st))
        return;
    const SDL_Color accent = FirmwareByName(st.firmware).accent;
    const int       n      = int(st.sounds.size());
    const float     h      = std::max(n, 1) * kSoundRowH + 2 * kSoundListPad;
    cv.RoundRect(kSoundX - 1, kBarH - 1, kSoundListW + 2, h + 2, 5.f, kMapEdge);
    cv.RoundRect(kSoundX, kBarH, kSoundListW, h, 4.f, kMapBox);
    for(int i = 0; i < n; i++)
    {
        const SDL_FRect r        = SoundRowRect(i);
        const bool      selected = st.sounds[i].index == st.sound;
        const bool      hover    = st.hover == Hit{HitKind::SoundRow, i};
        if(selected || hover)
            cv.RoundRect(r.x + 2, r.y, r.w - 4, r.h, 3.f, selected ? accent : kTabEdge);
        cv.Text(r.x + 8, r.y + 3, 1, selected ? kTabOnText : kMapText, SoundLabel(st.sounds[i], r.w - 16));
    }
    if(n == 0)
        cv.Text(kSoundX + 8, kBarH + kSoundListPad + 3, 1, kTabDim, "no sounds on the card");
}

/** A bar button: filled with the accent when on, outlined otherwise, dim when it cannot be used. */
void DrawBarButton(Canvas& cv, const SDL_FRect& r, const char* label, bool on, bool enabled, bool hover, SDL_Color accent)
{
    if(on)
        cv.RoundRect(r.x, r.y, r.w, r.h, 4.f, accent);
    else
    {
        cv.RoundRect(r.x, r.y, r.w, r.h, 4.f, hover && enabled ? accent : kTabEdge);
        cv.RoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 3.f, kBarBg);
    }
    cv.Text(r.x + r.w / 2, r.y + 5, 1, on ? kTabOnText : enabled ? kTabText : kTabDim, label, 0);
}

void DrawBar(Canvas& cv, const UiState& st)
{
    cv.FillRect(0, 0, float(kPanelW), kBarH, kBarBg);
    cv.Text(kTextX, kTabY + 5, 1, kHintColor, "FIRMWARE");
    const int n = int(sizeof(kFirmwares) / sizeof(kFirmwares[0]));
    for(int i = 0; i < n; i++)
    {
        const FirmwareInfo& f     = kFirmwares[i];
        bool                built = std::find(st.firmwares_built.begin(), st.firmwares_built.end(), f.id) != st.firmwares_built.end();
        DrawBarButton(cv, TabRect(i), f.name, st.firmware == f.id, built, st.hover == Hit{HitKind::FirmwareTab, i}, f.accent);
    }
    // the firmware's name and tagline; with a sound list the SOUND menu takes the tagline's place
    const FirmwareInfo& cur = FirmwareByName(st.firmware);
    const SDL_Color     accent = cur.name[0] ? cur.accent : SDL_Color{118, 122, 138, 255};
    std::string         line = cur.name[0] ? std::string(cur.name) + " " + cur.version + (cur.sound_cc ? "" : std::string("  ") + cur.tagline)
                                           : (st.firmware.empty() ? "" : st.firmware + " build");
    cv.Text(TabRect(n - 1).x + kTabW + 14, kTabY + 5, 1, kBarText, line);
    if(cur.sound_cc)
        DrawSoundButton(cv, st, accent);

    // the inputs: a sound file, the computer's microphone, the aux jack, and a level meter
    cv.Text(kInputX, kTabY + 5, 1, kHintColor, "INPUT");
    const bool  loaded    = !st.input.name.empty();
    const char* labels[4] = {"LOAD", st.input.playing ? "STOP" : "PLAY", "MIC", st.input.line_in ? "JACK: AUX" : "JACK: MIC"};
    const bool  on[4]     = {false, st.input.playing, st.mic_open, st.input.line_in};
    const bool  enabled[4] = {true, loaded, true, true};
    for(int i = 0; i < 4; i++)
        DrawBarButton(cv, InputButtonRect(i), labels[i], on[i], enabled[i], st.hover == Hit{HitKind::InputButton, i}, accent);
    cv.RoundRect(kMeterX, kTabY + 4, kMeterW, kTabH - 8, 3.f, kTabEdge);
    cv.RoundRect(kMeterX + 1, kTabY + 5, kMeterW - 2, kTabH - 10, 2.f, kBarBg);
    const float lvl = std::clamp(st.input_level, 0.f, 1.f);
    if(lvl > 0.f)
        cv.RoundRect(kMeterX + 2, kTabY + 6, (kMeterW - 4) * lvl, kTabH - 12, 2.f,
                     lvl > 0.95f ? SDL_Color{236, 72, 60, 255} : SDL_Color{96, 206, 120, 255});
    const SDL_FRect   help = HelpButtonRect();
    const std::string card = st.card_name.empty() ? "" : "card: " + st.card_name;
    const float       cardX = help.x - 8 - float(TextWidth(card, 1)); // right-aligned before the ?
    if(loaded)
    {
        std::string name = st.input.name.size() > 14 ? st.input.name.substr(0, 12) + ".." : st.input.name;
        char        t[48];
        std::snprintf(t, sizeof t, " %.1f/%.1fs", st.input.position_s, st.input.length_s);
        cv.Text(kMeterX + kMeterW + 8, kTabY + 5, 1, kBarText, Fit(name + t, cardX - 8 - (kMeterX + kMeterW + 8)));
    }
    if(!card.empty())
        cv.Text(cardX, kTabY + 5, 1, kHintColor, card);
    DrawBarButton(cv, help, "?", st.tour_step >= 0, true, st.hover == Hit{HitKind::HelpButton, 0}, accent);
}


// ---------------------------------------------------------------------------
// The guided tour (tour.h): the window shaded, the step's control drawn on
// top with a frame around it, and a note with an arrow to it
// ---------------------------------------------------------------------------
constexpr float     kNoteW      = 440.f;
constexpr float     kNotePad    = 10.f;
constexpr float     kNoteLineH  = 10.f; /**< body lines, 1 px font */
constexpr float     kNoteTitleH = 14.f; /**< 2 px font */
constexpr float     kNoteGap    = 24.f; /**< between the note and its target: room for the arrow */
constexpr float     kFramePad   = 6.f;
constexpr SDL_Color kNoteBg     = {30, 32, 40, 250};
constexpr SDL_Color kNoteText   = {236, 236, 230, 255};
constexpr SDL_Color kNoteDim    = {150, 154, 168, 255};
constexpr uint8_t   kTourDim    = 150; /**< alpha of the shade over the window */

/** Semitone of the i-th white or black key, left to right. */
int WhiteSemitone(int i)
{
    int n = 0;
    for(int s = 0; s < 25; s++)
        if(!kPianoGeom[s].upper && n++ == i)
            return s;
    return 24;
}
int BlackSemitone(int i)
{
    int n = 0;
    for(int s = 0; s < 25; s++)
        if(kPianoGeom[s].upper && n++ == i)
            return s;
    return 1;
}

void Unite(SDL_FRect& a, const SDL_FRect& b)
{
    if(a.w <= 0)
    {
        a = b;
        return;
    }
    const float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    const float x1 = std::max(a.x + a.w, b.x + b.w), y1 = std::max(a.y + a.h, b.y + b.h);
    a = {x0, y0, x1 - x0, y1 - y0};
}

/** The panel LED above a small knob, by PanelLed index; -1 for the transport knob. */
int LedOfKnob(int enc)
{
    switch(enc)
    {
        case ENC_SW4: return 1;
        case ENC_SW1: return 2;
        case ENC_SW2: return 3;
        case ENC_SW3: return 4;
        case ENC_SW6: return 9;
        default: return -1;
    }
}
int LedOfFuncKey(int button)
{
    return button == KEY_CHOMPI ? 0 : button == KEY_PLAY ? 7 : 8;
}
SDL_FRect LedRect(int i)
{
    const PanelLedDef& l = kPanelLeds[i];
    const float        r = l.r_mm * kMmPx + 2;
    return SDL_FRect{X(l.x_mm) - r, Y(l.y_mm) - r, 2 * r, 2 * r};
}
void DrawPanelLed(Canvas& cv, int i)
{
    DrawLed(cv, X(kPanelLeds[i].x_mm), Y(kPanelLeds[i].y_mm), kPanelLeds[i].r_mm * kMmPx, Sim::Get().PanelLed(i));
}

/** Where a target is on the canvas (w <= 0: the step has none). */
SDL_FRect TargetRect(const TourTarget& t)
{
    SDL_FRect r{0, 0, 0, 0};
    switch(t.kind)
    {
        case TourTarget::Toggle:
        {
            const ToggleDef& g = kToggle;
            r = {X(g.x_mm) - g.thumb_w / 2, Y(g.y_mm) - g.h / 2, g.thumb_w, g.h + 16}; // with its label
            break;
        }
        case TourTarget::FuncKey:
            for(const FuncKeyDef& f : kFuncKeys)
                if(f.button == t.a)
                {
                    r = FuncKeyRect(f);
                    r.h += 12; // the cue under it
                    Unite(r, LedRect(LedOfFuncKey(f.button)));
                }
            break;
        case TourTarget::Knob:
            for(const KnobDef& k : kKnobs)
                if(k.enc == t.a)
                {
                    const float R = k.r_mm * kMmPx;
                    r             = {X(k.x_mm) - R, Y(k.y_mm) - R, 2 * R, 2 * R + 24}; // with its labels
                    if(k.big)
                    {
                        Unite(r, LedRect(5));
                        Unite(r, LedRect(6));
                    }
                    else
                        Unite(r, LedRect(LedOfKnob(k.enc)));
                }
            break;
        case TourTarget::WhiteKeys:
            for(int i = t.a; i <= t.b; i++)
                Unite(r, PianoKeyRect(WhiteSemitone(i)));
            break;
        case TourTarget::BlackKeys:
            for(int i = t.a; i <= t.b; i++)
                Unite(r, PianoKeyRect(BlackSemitone(i)));
            break;
        case TourTarget::Keyboard:
            for(int s = 0; s < 25; s++)
                Unite(r, PianoKeyRect(s));
            break;
        case TourTarget::PanelLeds:
            for(int i = 0; i < kNumPanelLeds; i++)
                Unite(r, LedRect(i));
            break;
        case TourTarget::Tabs:
            for(int i = 0; i < int(sizeof(kFirmwares) / sizeof(kFirmwares[0])); i++)
                Unite(r, TabRect(i));
            r.x -= 72; // with the FIRMWARE label
            r.w += 72;
            break;
        case TourTarget::Input: r = {kInputX, kTabY, kMeterX + kMeterW - kInputX, kTabH}; break;
        case TourTarget::Sound:
            r = SoundButtonRect();
            r.w += r.x - kSoundLabelX;
            r.x = kSoundLabelX;
            break;
        case TourTarget::Help: r = HelpButtonRect(); break;
        case TourTarget::None: break;
    }
    return r;
}

/** The step's control, drawn again over the shade so it stands out. */
void DrawTargetControls(Canvas& cv, const TourTarget& t, const UiState& st)
{
    switch(t.kind)
    {
        case TourTarget::Toggle: DrawToggle(cv, st); break;
        case TourTarget::FuncKey:
            for(const FuncKeyDef& f : kFuncKeys)
                if(f.button == t.a)
                {
                    DrawFuncKey(cv, f, st);
                    DrawPanelLed(cv, LedOfFuncKey(f.button));
                }
            break;
        case TourTarget::Knob:
            for(const KnobDef& k : kKnobs)
                if(k.enc == t.a)
                {
                    DrawKnob(cv, k, st);
                    if(k.big)
                    {
                        DrawPanelLed(cv, 5);
                        DrawPanelLed(cv, 6);
                    }
                    else
                        DrawPanelLed(cv, LedOfKnob(k.enc));
                }
            break;
        case TourTarget::WhiteKeys:
            for(int i = t.a; i <= t.b; i++)
                DrawPianoKey(cv, WhiteSemitone(i), st);
            break;
        case TourTarget::BlackKeys:
            for(int i = t.a; i <= t.b; i++)
                DrawPianoKey(cv, BlackSemitone(i), st);
            break;
        case TourTarget::Keyboard:
            for(int s = 0; s < 25; s++)
                DrawPianoKey(cv, s, st);
            break;
        case TourTarget::PanelLeds:
            for(int i = 0; i < kNumPanelLeds; i++)
                DrawPanelLed(cv, i);
            break;
        default: break; // the bar is not shaded
    }
}

void DrawFrame(Canvas& cv, SDL_FRect r, SDL_Color c)
{
    r.x -= kFramePad;
    r.y -= kFramePad;
    r.w += 2 * kFramePad;
    r.h += 2 * kFramePad;
    cv.Line(r.x, r.y, r.x + r.w, r.y, 2.5f, c);
    cv.Line(r.x + r.w, r.y, r.x + r.w, r.y + r.h, 2.5f, c);
    cv.Line(r.x + r.w, r.y + r.h, r.x, r.y + r.h, 2.5f, c);
    cv.Line(r.x, r.y + r.h, r.x, r.y, 2.5f, c);
}

/** Word-wraps a text to `chars` columns; a newline in it starts a new line. */
std::vector<std::string> Wrap(const char* text, int chars)
{
    std::vector<std::string> lines;
    if(!text)
        return lines;
    std::string line, word;
    auto        flush = [&]() {
        if(word.empty())
            return;
        if(!line.empty() && int(line.size() + 1 + word.size()) > chars)
        {
            lines.push_back(line);
            line.clear();
        }
        if(!line.empty())
            line += ' ';
        line += word;
        word.clear();
    };
    for(const char* p = text; *p; p++)
    {
        if(*p == ' ')
            flush();
        else if(*p == '\n')
        {
            flush();
            lines.push_back(line);
            line.clear();
        }
        else
            word += *p;
    }
    flush();
    if(!line.empty())
        lines.push_back(line);
    return lines;
}

/** Where the note of the current step and its buttons go. */
struct NoteLayout
{
    const TourStep*          step = nullptr;
    int                      index = 0, count = 0;
    SDL_FRect                box{0, 0, 0, 0}, back{0, 0, 0, 0}, next{0, 0, 0, 0}, close{0, 0, 0, 0};
    SDL_FRect                target{0, 0, 0, 0};
    bool                     below = false; /**< the note sits below its target */
    std::vector<std::string> body, keys;
};

NoteLayout LayoutNote(const UiState& st)
{
    NoteLayout L;
    const Tour tour = TourFor(st.firmware);
    if(st.tour_step < 0 || tour.count == 0)
        return L;
    L.index = std::clamp(st.tour_step, 0, tour.count - 1);
    L.count = tour.count;
    L.step  = &tour.steps[L.index];
    const int chars = int((kNoteW - 2 * kNotePad) / float(kGlyphAdv));
    L.body          = Wrap(L.step->body, chars);
    L.keys          = Wrap(L.step->keys, chars);
    const float h = kNotePad + kNoteTitleH + 6 + L.body.size() * kNoteLineH
                    + (L.keys.empty() ? 0.f : 4 + L.keys.size() * kNoteLineH) + 10 + kTabH + kNotePad;
    L.target = TargetRect(L.step->target);
    float x, y;
    if(L.target.w > 0)
    {
        const float cx = L.target.x + L.target.w / 2, cy = L.target.y + L.target.h / 2;
        L.below        = cy < kBarH + 150.f; // the top row and the bar: the note goes under them
        y              = L.below ? L.target.y + L.target.h + kFramePad + kNoteGap : L.target.y - kFramePad - kNoteGap - h;
        x              = cx - kNoteW / 2;
    }
    else
    {
        x = (float(kPanelW) - kNoteW) / 2;
        y = kBarH + 70;
    }
    x     = std::clamp(x, 8.f, float(kPanelW) - 8 - kNoteW);
    y     = std::clamp(y, kBarH + 6, float(kPanelH) - 6 - h);
    L.box = {x, y, kNoteW, h};
    const float by = y + h - kNotePad - kTabH;
    L.back         = {x + kNotePad, by, 52, kTabH};
    L.next         = {x + kNotePad + 58, by, 56, kTabH};
    L.close        = {x + kNoteW - kNotePad - 72, by, 72, kTabH};
    return L;
}

void DrawTour(Canvas& cv, const UiState& st)
{
    const NoteLayout L = LayoutNote(st);
    if(!L.step)
        return;
    const FirmwareInfo& fw     = FirmwareByName(st.firmware);
    const SDL_Color     accent = fw.name[0] ? fw.accent : SDL_Color{118, 122, 138, 255};
    cv.FillRect(0, kBarH, float(kPanelW), float(kPanelH) - kBarH, SDL_Color{0, 0, 0, kTourDim});
    DrawTargetControls(cv, L.step->target, st);
    if(L.target.w > 0)
        DrawFrame(cv, L.target, accent);

    const SDL_FRect& b = L.box;
    cv.RoundRect(b.x - 2, b.y - 2, b.w + 4, b.h + 4, 8.f, accent);
    cv.RoundRect(b.x, b.y, b.w, b.h, 6.f, kNoteBg);
    float y = b.y + kNotePad;
    if(float(TextWidth(L.step->title, 2)) <= b.w - 2 * kNotePad - 70) // room for the counter
        cv.Text(b.x + kNotePad, y, 2, accent, L.step->title);
    else
        cv.Text(b.x + kNotePad, y + 4, 1, accent, L.step->title);
    cv.Text(b.x + b.w - kNotePad, y + 4, 1, kNoteDim, std::to_string(L.index + 1) + " / " + std::to_string(L.count), 1);
    y += kNoteTitleH + 6;
    for(const std::string& line : L.body)
    {
        cv.Text(b.x + kNotePad, y, 1, kNoteText, line);
        y += kNoteLineH;
    }
    if(!L.keys.empty())
    {
        y += 4;
        for(const std::string& line : L.keys)
        {
            cv.Text(b.x + kNotePad, y, 1, kMapKey, line);
            y += kNoteLineH;
        }
    }
    const bool last = L.index + 1 >= L.count;
    DrawBarButton(cv, L.back, "< BACK", false, L.index > 0, st.hover == Hit{HitKind::TourBack, 0}, accent);
    DrawBarButton(cv, L.next, last ? "DONE" : "NEXT >", true, true, false, accent);
    if(!last)
        DrawBarButton(cv, L.close, "SKIP TOUR", false, true, st.hover == Hit{HitKind::TourClose, 0}, accent);
    cv.Text(L.next.x + L.next.w + 10, L.next.y + 5, 1, kNoteDim, "click or > = next   < = back   ESC");

    if(L.target.w > 0)
    {
        const float tx = L.target.x + L.target.w / 2;
        const float ax = std::clamp(tx, b.x + 24, b.x + b.w - 24);
        if(L.below)
            cv.Arrow(ax, b.y - 3, tx, L.target.y + L.target.h + kFramePad + 4, accent);
        else
            cv.Arrow(ax, b.y + b.h + 3, tx, L.target.y - kFramePad - 4, accent);
    }
}

/** While the tour is up every click is for it: its buttons, or "next". */
Hit TourHit(const UiState& st, float x, float y)
{
    const NoteLayout L = LayoutNote(st);
    if(!L.step)
        return Hit{HitKind::TourNext, 0};
    if(L.index > 0 && Contains(L.back, x, y))
        return Hit{HitKind::TourBack, 0};
    if(L.index + 1 < L.count && Contains(L.close, x, y))
        return Hit{HitKind::TourClose, 0};
    return Hit{HitKind::TourNext, 0};
}

} // namespace

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------
Panel::Panel(SDL_Renderer* renderer, float scale) : cv_(std::make_unique<Canvas>(renderer, scale)) {}
Panel::~Panel() = default;

void Panel::Draw(const UiState& st)
{
    Canvas& cv = *cv_;
    cv.Clear(kBg);
    cv.RoundRect(kBody.x - 1, kBody.y - 1, kBody.w + 2, kBody.h + 2, 15, kBodyEdge);
    cv.RoundRect(kBody.x, kBody.y, kBody.w, kBody.h, 14, kBodyColor);
    DrawPianoKeys(cv, st);
    for(const FuncKeyDef& f : kFuncKeys)
        DrawFuncKey(cv, f, st);
    for(const KnobDef& k : kKnobs)
        DrawKnob(cv, k, st);
    for(int i = 0; i < kNumPanelLeds; i++)
        DrawLed(cv, X(kPanelLeds[i].x_mm), Y(kPanelLeds[i].y_mm), kPanelLeds[i].r_mm * kMmPx, Sim::Get().PanelLed(i));
    DrawToggle(cv, st);
    if(st.keymap)
        DrawKeyMap(cv, st);
    DrawBar(cv, st);
    DrawTextArea(cv, st);
    DrawSoundMenu(cv, st);
    DrawTour(cv, st);
}

Hit Panel::HitTest(const UiState& st, float x, float y) const
{
    if(st.tour_step >= 0)
        return TourHit(st, x, y);
    if(st.sound_menu && HasSoundMenu(st))
        for(int i = 0; i < int(st.sounds.size()); i++)
            if(Contains(SoundRowRect(i), x, y))
                return Hit{HitKind::SoundRow, i};
    if(y < kBarH)
    {
        for(int i = 0; i < int(sizeof(kFirmwares) / sizeof(kFirmwares[0])); i++)
            if(Contains(TabRect(i), x, y))
                return Hit{HitKind::FirmwareTab, i};
        if(HasSoundMenu(st) && Contains(SoundButtonRect(), x, y))
            return Hit{HitKind::SoundButton, 0};
        if(Contains(HelpButtonRect(), x, y))
            return Hit{HitKind::HelpButton, 0};
        for(int i = 0; i < 4; i++)
            if(Contains(InputButtonRect(i), x, y))
                return Hit{HitKind::InputButton, i};
        return Hit{};
    }
    for(int semi = 0; semi < 25; semi++)
        if(Contains(PianoKeyRect(semi), x, y))
            return Hit{HitKind::PianoKey, semi};
    for(const KnobDef& k : kKnobs)
    {
        float dx = x - X(k.x_mm), dy = y - Y(k.y_mm), rr = k.r_mm * kMmPx + 4;
        if(dx * dx + dy * dy <= rr * rr)
            return Hit{HitKind::Knob, k.enc};
    }
    for(const FuncKeyDef& f : kFuncKeys)
        if(Contains(FuncKeyRect(f), x, y))
            return Hit{HitKind::FuncKey, f.button};
    if(Contains(ToggleHitRect(), x, y))
        return Hit{HitKind::Toggle, 0};
    return Hit{};
}

} // namespace gui
