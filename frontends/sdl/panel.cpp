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
#include "font.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gui
{
using namespace chompi_sim;

const char* const kPianoKeyNames[25] = {"Z", "S", "X", "D", "C", "V", "G", "B", "H", "N", "J", "M", "Q",
                                        "2", "W", "3", "E", "R", "5", "T", "6", "Y", "7", "U", "I"};

namespace
{

// ---------------------------------------------------------------------------
// Board geometry (millimetres, from hardware/hardware-pcb CC_Chompi_Rev4.brd)
// mapped onto the logical canvas (kPanelW x kPanelH at --scale 1)
// ---------------------------------------------------------------------------
constexpr float kMmPx      = 3.4f;   /**< logical pixels per millimetre */
constexpr float kBoardX    = 16.f;   /**< canvas x of board x = 0 */
constexpr float kBoardTopY = 14.f;   /**< canvas y of the board's top edge */
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

constexpr SDL_FRect kBody = {6, 2, 1108, 348}; /**< the instrument enclosure */

/** Text rows (logical y of the top of the text). */
constexpr float kHintY1  = 356;
constexpr float kHintY2  = 366;
constexpr float kStatusY = 380;
constexpr float kLogY    = 392;
constexpr float kLogDy   = 10;
constexpr float kTextX   = 16;

const char* const kHint1 = "/ or ? = key map   PIANO  z s x d c v g b h n j m = lower octave   q 2 w 3 e r 5 t 6 y 7 u i = upper   "
                           "SPACE play   RETURN loop   L-SHIFT chompi (hold)   TAB mode   ESC quit";
const char* const kHint2 = "KNOBS  drag or scroll = turn   click = push   right-click = hold   [ ] transport   - = volume   "
                           "LEFT/RIGHT last small knob   F1-F6 push   F7/F8 play/stop input sound";
/** Hint lines while the key map overlay is up. */
const char* const kMapHint1 = "KEY MAP   the letters on the caps play the notes (two octaves)   LEFT/RIGHT arrows turn the small knob "
                              "touched last   / or ? hides this map";
const char* const kMapHint2 = "MOUSE   click or hold any key   drag up/down or scroll on a knob = turn   click a knob = push   "
                              "right-click = hold   F7/F8 play/stop the input sound   ESC quit";

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

void DrawPianoKeys(Canvas& cv, const UiState& st)
{
    for(int semi = 0; semi < 25; semi++)
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
    }
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

    if(!st.keymap) // the key map names the knobs in its callouts
        cv.Text(cx, cy + R + 6, 1, kLabel, k.label, 0);
}

void DrawFuncKey(Canvas& cv, const FuncKeyDef& f, const UiState& st)
{
    SDL_FRect r       = FuncKeyRect(f);
    bool      pressed = Sim::Get().ButtonPressed(f.button);
    bool      hover   = st.hover == Hit{HitKind::FuncKey, f.button};
    float     dy      = pressed ? kKeyPressDy : 0.f;
    DrawKeyCap(cv, r, kFuncFace, kFuncSide, pressed, hover);
    cv.Text(r.x + r.w / 2, r.y + r.h / 2 - 5 + dy, 1, kFuncText, f.label, 0);
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
    if(!st.keymap)
        cv.Text(cx, cy + t.h / 2 + 8, 1, kLabel, "MODE", 0);
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

    for(int semi = 0; semi < 25; semi++)
    {
        SDL_FRect r  = PianoKeyRect(semi);
        float     dy = Sim::Get().ButtonPressed(kPianoKeys[semi]) ? kKeyPressDy : 0.f;
        DrawKeyChip(cv, r.x + r.w / 2, r.y + r.h - kKeyLipH - kChipH / 2 - 3 + dy, kPianoKeyNames[semi]);
    }

    // Boxes sit in two free bands: above the LED row and between the knobs
    // and the upper caps. Arrows that would cross an LED leave the box off
    // centre and land on the knob's shoulder.
    constexpr float kBandTop = 5.f;
    constexpr float kBandMid = 164.f;
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
        callouts.push_back({f.label, f.key, cx + (f.button == KEY_CHOMPI ? 4.f : 0.f), kBandTop, ax, true, ax, r.y - 2});
    }
    for(const KnobDef& k : kKnobs)
    {
        float       cx = X(k.x_mm), cy = Y(k.y_mm), R = k.r_mm * kMmPx;
        std::string keys = k.ccw_key ? std::string(k.ccw_key) + " " + k.cw_key + " turn  " + k.push_key + " push"
                                     : std::string(k.push_key) + " push";
        if(!k.ccw_key && st.arrow_knob == k.enc)
            keys += "  < > turn";
        if(k.enc == ENC_SW4 || k.enc == ENC_SW2 || k.enc == ENC_SW6)
            callouts.push_back({k.label, keys, cx, kBandMid, cx, false, cx, cy + R + 2});
        else if(k.big)
            callouts.push_back({k.label, keys, cx, kBandTop, cx, true, cx, cy - R - 2});
        else
            callouts.push_back({k.label, keys, cx, kBandTop, cx + 28.f, true, cx + R * 0.5f + 1, cy - R * 0.866f - 1});
    }
    for(const Callout& c : callouts)
        DrawCallout(cv, c);
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
    DrawTextArea(cv, st);
}

Hit Panel::HitTest(float x, float y) const
{
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
