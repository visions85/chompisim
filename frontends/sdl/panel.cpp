/** @file panel.cpp
 *  @brief Layout tables, sprite-based drawing helpers and the panel renderer.
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

const char* const kPianoKeyNames[25] = {"z", "s", "x", "d", "c", "v", "g", "b", "h", "n", "j", "m", "q",
                                        "2", "w", "3", "e", "r", "5", "t", "6", "y", "7", "u", "i"};

namespace
{

// ---------------------------------------------------------------------------
// Layout, in logical pixels (window = kPanelW x kPanelH at --scale 1)
// ---------------------------------------------------------------------------

struct KnobDef
{
    int         enc;   /**< Encoder */
    int         led;   /**< PanelLed of the ring */
    const char* label;
    float       cx, cy, r;
};
constexpr KnobDef kKnobs[] = {
    {ENC_SW5, PANEL_LED_TRANSPORT_KNOB, "TRANSPORT", 108, 92, 56},
    {ENC_SW4, PANEL_LED_PITCH_KNOB, "PITCH", 612, 92, 29},
    {ENC_SW1, PANEL_LED_KNOB_A, "A", 708, 92, 29},
    {ENC_SW2, PANEL_LED_KNOB_B, "B", 804, 92, 29},
    {ENC_SW3, PANEL_LED_KNOB_C, "C", 900, 92, 29},
    {ENC_SW6, PANEL_LED_VOLUME_KNOB, "VOLUME", 1040, 92, 35},
};
/** Outer radius of the LED ring around a knob of radius r. */
float RingRadius(float r)
{
    return r * 1.14f + 1.f;
}

struct FuncKeyDef
{
    int         button; /**< Button */
    int         led;    /**< PanelLed or -1 */
    const char* label;
    float       x, y, w, h;
};
constexpr FuncKeyDef kFuncKeys[] = {
    {KEY_PLAY, PANEL_LED_PLAY, "PLAY", 222, 62, 60, 60},
    {KEY_LOOP, PANEL_LED_LOOP, "LOOP", 296, 62, 60, 60},
    {KEY_CHOMPI, -1, "CHOMPI", 376, 62, 120, 60},
};

struct IndicatorDef
{
    int   led;
    float cx, cy, r;
};
constexpr IndicatorDef kIndicators[] = {
    {PANEL_LED_INDICATOR_A, 196, 80, 5},
    {PANEL_LED_INDICATOR_B, 196, 106, 5},
};

struct ToggleDef
{
    float       cx, cy, w, h; /**< the slot */
    float       thumb_w, thumb_h;
    const char* label;
};
constexpr ToggleDef kToggle = {532, 92, 18, 50, 26, 20, "MODE"};

struct KeybedDef
{
    float x, y;
    float white_w, white_h;
    float black_w, black_h;
    float gap; /**< between white keys */
};
constexpr KeybedDef kKeybed    = {42, 186, 69, 152, 40, 92, 2};
constexpr int       kNumWhite  = 15;
constexpr int       kNumBlack  = 10;
constexpr int       kWhiteSemis[kNumWhite] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24};
constexpr int       kBlackSemis[kNumBlack] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
constexpr float     kKeyLipH   = 4; /**< visible side of an unpressed key cap */
constexpr float     kPi        = 3.14159265f;
constexpr float     kKeyPressDy = 3; /**< how far a pressed cap sinks */

constexpr SDL_FRect kBody = {10, 6, 1100, 340}; /**< the instrument enclosure */

/** Text rows (logical y of the top of the text). */
constexpr float kHintY1  = 352;
constexpr float kHintY2  = 362;
constexpr float kStatusY = 376;
constexpr float kLogY    = 388;
constexpr float kLogDy   = 10;
constexpr float kTextX   = 16;

const char* const kHint1 = "PIANO  z s x d c v g b h n j m = lower octave   q 2 w 3 e r 5 t 6 y 7 u i = upper octave   "
                           "SPACE play   RETURN loop   L-SHIFT chompi (hold)   TAB mode toggle   ESC quit";
const char* const kHint2 = "KNOBS  mouse wheel = turn   left click = push   [ ] transport   - = volume   "
                           "LEFT/RIGHT last small knob   F1-F6 push ENC1-ENC6 (hold)   mouse clicks press keys";

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------
constexpr SDL_Color kBg         = {16, 17, 20, 255};
constexpr SDL_Color kBodyColor  = {38, 40, 46, 255};
constexpr SDL_Color kBodyEdge   = {52, 55, 62, 255};
constexpr SDL_Color kSlot       = {20, 21, 25, 255};
constexpr SDL_Color kWhiteFace  = {226, 223, 214, 255};
constexpr SDL_Color kWhiteSide  = {160, 157, 148, 255};
constexpr SDL_Color kWhiteText  = {96, 94, 90, 255};
constexpr SDL_Color kBlackFace  = {40, 41, 47, 255};
constexpr SDL_Color kBlackSide  = {14, 15, 18, 255};
constexpr SDL_Color kBlackText  = {150, 152, 160, 255};
constexpr SDL_Color kFuncFace   = {66, 70, 80, 255};
constexpr SDL_Color kFuncSide   = {30, 32, 38, 255};
constexpr SDL_Color kFuncText   = {235, 236, 240, 255};
constexpr SDL_Color kKnobRim    = {78, 82, 92, 255};
constexpr SDL_Color kKnobRimHot = {120, 126, 140, 255};
constexpr SDL_Color kKnobBody   = {48, 51, 58, 255};
constexpr SDL_Color kKnobBodyDn = {34, 36, 42, 255};
constexpr SDL_Color kPointer    = {236, 236, 240, 255};
constexpr SDL_Color kLedOff     = {12, 13, 16, 255};
constexpr SDL_Color kRingOff    = {24, 25, 30, 255};
constexpr SDL_Color kLabel      = {168, 173, 184, 255};
constexpr SDL_Color kToggleSlot = {18, 19, 23, 255};
constexpr SDL_Color kToggleThumb = {200, 198, 190, 255};
constexpr SDL_Color kToggleGrip = {120, 118, 112, 255};
constexpr SDL_Color kHintColor  = {104, 110, 124, 255};
constexpr SDL_Color kStatusColor = {170, 176, 188, 255};
constexpr SDL_Color kLogColor   = {120, 170, 226, 255};

SDL_Color Scaled(SDL_Color c, float f)
{
    auto s = [f](uint8_t v) { return uint8_t(std::clamp(v * f, 0.f, 255.f)); };
    return SDL_Color{s(c.r), s(c.g), s(c.b), c.a};
}
float Brightness(Rgb c)
{
    return std::max({c.r, c.g, c.b}) / 255.f;
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
        // Anti-aliased disc, radius kDiscR of a kSprite-sized sprite.
        disc_ = MakeSprite([](float d) { return std::clamp(kDiscR + 0.5f - d, 0.f, 1.f); });
        // Annulus between kRingInner and kDiscR.
        ring_ = MakeSprite([](float d) {
            return std::clamp(kDiscR + 0.5f - d, 0.f, 1.f) * std::clamp(d - kRingInner + 0.5f, 0.f, 1.f);
        });
        // Soft halo: alpha falls off to zero at the sprite edge.
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

    /** Anti-aliased filled circle. */
    void Disc(float cx, float cy, float r, SDL_Color c)
    {
        Sprite(disc_, cx, cy, r * scale_ * (kSprite / 2.f) / kDiscR, c, SDL_BLENDMODE_BLEND);
    }

    /** Ring with outer radius r (inner radius 0.8 r). */
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

    /** Text with font pixel size `px` logical pixels; align -1 left, 0 centre, 1 right of x.
     *  Font pixels are whole device pixels, rounded down so text never grows
     *  beyond the room the logical layout gives it at fractional scales. */
    void Text(float x, float y, int px, SDL_Color c, const std::string& s, int align = -1)
    {
        int dpx = std::max(1, int(std::floor(px * scale_)));
        int w   = TextWidth(s, dpx);
        int dx  = int(std::lround(x * scale_)) - (align == 0 ? w / 2 : align > 0 ? w : 0);
        DrawText(r_, dx, int(std::lround(y * scale_)), dpx, c, s);
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

int WhiteIndexOf(int semitone)
{
    for(int i = 0; i < kNumWhite; i++)
        if(kWhiteSemis[i] == semitone)
            return i;
    return -1;
}

SDL_FRect WhiteKeyRect(int i)
{
    const KeybedDef& k = kKeybed;
    return SDL_FRect{k.x + i * k.white_w + k.gap / 2, k.y, k.white_w - k.gap, k.white_h};
}

SDL_FRect BlackKeyRect(int i)
{
    const KeybedDef& k  = kKeybed;
    int              wi = WhiteIndexOf(kBlackSemis[i] - 1); // white key to the left
    float            bx = k.x + (wi + 1) * k.white_w - k.black_w / 2;
    return SDL_FRect{bx, k.y, k.black_w, k.black_h};
}

bool Contains(const SDL_FRect& r, float x, float y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

SDL_FRect ToggleHitRect()
{
    const ToggleDef& t = kToggle;
    return SDL_FRect{t.cx - t.thumb_w / 2 - 4, t.cy - t.h / 2 - 4, t.thumb_w + 8, t.h + 8};
}

// ---------------------------------------------------------------------------
// Drawing of the individual controls
// ---------------------------------------------------------------------------

/** A key cap: a top face sitting on a darker side; pressed caps sink and lose the side. */
void DrawKeyCap(Canvas& cv, const SDL_FRect& r, SDL_Color face, SDL_Color side, bool pressed, bool hover)
{
    float lip = pressed ? 1.f : kKeyLipH;
    float dy  = pressed ? kKeyPressDy : 0.f;
    cv.FillRect(r.x, r.y + r.h - lip, r.w, lip, side);
    SDL_Color f = pressed ? Scaled(face, 0.86f) : hover ? Scaled(face, 1.08f) : face;
    cv.FillRect(r.x, r.y + dy, r.w, r.h - lip - dy, f);
}

/** An LED: a dark lens, the lit colour, and a halo. */
void DrawLed(Canvas& cv, float cx, float cy, float r, float glowR, Rgb c, bool additiveGlow)
{
    cv.Disc(cx, cy, r, kLedOff);
    if(Brightness(c) <= 0.f)
        return;
    cv.Disc(cx, cy, r, SDL_Color{c.r, c.g, c.b, 255});
    cv.Glow(cx, cy, glowR, c, 0.9f, additiveGlow);
}

void DrawKeybed(Canvas& cv, const UiState& st)
{
    const KeybedDef& k = kKeybed;
    cv.RoundRect(k.x - 6, k.y - 6, kNumWhite * k.white_w + 12, k.white_h + 12, 6, kSlot);
    for(int i = 0; i < kNumWhite; i++)
    {
        int       semi    = kWhiteSemis[i];
        SDL_FRect r       = WhiteKeyRect(i);
        bool      pressed = Sim::Get().ButtonPressed(kPianoKeys[semi]);
        bool      hover   = st.hover == Hit{HitKind::PianoKey, semi};
        float     dy      = pressed ? kKeyPressDy : 0.f;
        DrawKeyCap(cv, r, kWhiteFace, kWhiteSide, pressed, hover);
        DrawLed(cv, r.x + r.w / 2, r.y + r.h - 36 + dy, 7, 28, Sim::Get().KeyLed(kPianoKeyLed[semi]), false);
        cv.Text(r.x + r.w / 2, r.y + r.h - 20 + dy, 1, kWhiteText, kPianoKeyNames[semi], 0);
    }
    for(int i = 0; i < kNumBlack; i++)
    {
        int       semi    = kBlackSemis[i];
        SDL_FRect r       = BlackKeyRect(i);
        bool      pressed = Sim::Get().ButtonPressed(kPianoKeys[semi]);
        bool      hover   = st.hover == Hit{HitKind::PianoKey, semi};
        float     dy      = pressed ? kKeyPressDy : 0.f;
        DrawKeyCap(cv, r, kBlackFace, kBlackSide, pressed, hover);
        DrawLed(cv, r.x + r.w / 2, r.y + r.h - 32 + dy, 6, 22, Sim::Get().KeyLed(kPianoKeyLed[semi]), true);
        cv.Text(r.x + r.w / 2, r.y + r.h - 18 + dy, 1, kBlackText, kPianoKeyNames[semi], 0);
    }
}

void DrawKnob(Canvas& cv, const KnobDef& k, const UiState& st)
{
    float ringR   = RingRadius(k.r);
    Rgb   led     = Sim::Get().PanelLed(k.led);
    bool  pressed = st.knob_pressed[size_t(k.enc)];
    bool  hover   = st.hover == Hit{HitKind::Knob, k.enc};

    // LED ring: dark when off, coloured with a halo when lit.
    cv.Ring(k.cx, k.cy, ringR, kRingOff);
    if(Brightness(led) > 0.f)
    {
        cv.Ring(k.cx, k.cy, ringR, SDL_Color{led.r, led.g, led.b, 255});
        cv.Glow(k.cx, k.cy, ringR * 1.75f, led, 0.75f, true);
    }

    // Body: rim + cap. A pushed knob looks smaller and darker.
    float r = pressed ? k.r * 0.94f : k.r;
    cv.Disc(k.cx, k.cy, r, hover ? kKnobRimHot : kKnobRim);
    cv.Disc(k.cx, k.cy, r - 3, pressed ? kKnobBodyDn : kKnobBody);

    // Position mark: a thick radial line made of overlapping discs.
    float a  = st.knob_angle[size_t(k.enc)] * kPi / 180.f;
    float sx = std::sin(a), sy = -std::cos(a);
    float pw = std::max(1.6f, r * 0.06f);
    for(float t = 0.34f; t <= 0.84f; t += 0.04f)
        cv.Disc(k.cx + sx * t * r, k.cy + sy * t * r, pw, kPointer);

    cv.Text(k.cx, k.cy + ringR + 7, 2, kLabel, k.label, 0);
}

void DrawIndicator(Canvas& cv, const IndicatorDef& d)
{
    DrawLed(cv, d.cx, d.cy, d.r, d.r * 3.6f, Sim::Get().PanelLed(d.led), true);
}

void DrawFuncKey(Canvas& cv, const FuncKeyDef& f, const UiState& st)
{
    SDL_FRect r       = {f.x, f.y, f.w, f.h};
    bool      pressed = Sim::Get().ButtonPressed(f.button);
    bool      hover   = st.hover == Hit{HitKind::FuncKey, f.button};
    float     dy      = pressed ? kKeyPressDy : 0.f;
    DrawKeyCap(cv, r, kFuncFace, kFuncSide, pressed, hover);
    cv.Text(f.x + f.w / 2, f.y + f.h / 2 - 7 - kKeyLipH / 2 + dy, 2, kFuncText, f.label, 0);
    if(f.led >= 0)
        DrawLed(cv, f.x + f.w / 2, f.y - 11, 5, 18, Sim::Get().PanelLed(f.led), true);
}

void DrawToggle(Canvas& cv, const UiState& st)
{
    const ToggleDef& t    = kToggle;
    bool             down = Sim::Get().ToggleDown();
    bool             hover = st.hover == Hit{HitKind::Toggle, 0};
    cv.RoundRect(t.cx - t.w / 2, t.cy - t.h / 2, t.w, t.h, t.w / 2, kToggleSlot);
    float ty = down ? t.cy + t.h / 2 - t.thumb_h - 1 : t.cy - t.h / 2 + 1;
    cv.RoundRect(t.cx - t.thumb_w / 2, ty, t.thumb_w, t.thumb_h, 5,
                 hover ? Scaled(kToggleThumb, 1.1f) : kToggleThumb);
    cv.FillRect(t.cx - t.thumb_w / 2 + 6, ty + t.thumb_h / 2 - 1, t.thumb_w - 12, 2, kToggleGrip);
    cv.Text(t.cx, t.cy + t.h / 2 + 9, 2, kLabel, t.label, 0);
}

void DrawTextArea(Canvas& cv, const UiState& st)
{
    cv.Text(kTextX, kHintY1, 1, kHintColor, kHint1);
    cv.Text(kTextX, kHintY2, 1, kHintColor, kHint2);
    cv.Text(kTextX, kStatusY, 1, kStatusColor, st.status);
    float y = kLogY;
    for(const std::string& line : st.log)
    {
        cv.Text(kTextX, y, 1, kLogColor, "> " + line);
        y += kLogDy;
    }
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
    DrawKeybed(cv, st);
    for(const KnobDef& k : kKnobs)
        DrawKnob(cv, k, st);
    for(const IndicatorDef& d : kIndicators)
        DrawIndicator(cv, d);
    for(const FuncKeyDef& f : kFuncKeys)
        DrawFuncKey(cv, f, st);
    DrawToggle(cv, st);
    DrawTextArea(cv, st);
}

Hit Panel::HitTest(float x, float y) const
{
    // Black keys sit on top of the white keys, so test them first.
    for(int i = 0; i < kNumBlack; i++)
        if(Contains(BlackKeyRect(i), x, y))
            return Hit{HitKind::PianoKey, kBlackSemis[i]};
    for(int i = 0; i < kNumWhite; i++)
        if(Contains(WhiteKeyRect(i), x, y))
            return Hit{HitKind::PianoKey, kWhiteSemis[i]};
    for(const KnobDef& k : kKnobs)
    {
        float dx = x - k.cx, dy = y - k.cy, rr = RingRadius(k.r) + 2;
        if(dx * dx + dy * dy <= rr * rr)
            return Hit{HitKind::Knob, k.enc};
    }
    for(const FuncKeyDef& f : kFuncKeys)
        if(Contains(SDL_FRect{f.x, f.y, f.w, f.h}, x, y))
            return Hit{HitKind::FuncKey, f.button};
    if(Contains(ToggleHitRect(), x, y))
        return Hit{HitKind::Toggle, 0};
    return Hit{};
}

} // namespace gui
