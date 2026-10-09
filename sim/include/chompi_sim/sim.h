/** @file sim.h
 *  @brief Public API of the CHOMPI desktop simulator core.
 *
 *  The core runs the unmodified CHOMPI firmware (TAPE / TEMPO / WAVE / community
 *  builds) against a host implementation of libDaisy. Front-ends (the SDL window,
 *  the headless runner, tests) only talk to this header.
 *
 *  Threading model
 *  ---------------
 *  - The firmware's main() runs on its own thread ("firmware thread").
 *  - Everything the hardware would do in an interrupt (audio callback, hardware
 *    timers, DMA completion callbacks) runs inside RenderBlock(), i.e. on whatever
 *    thread calls RenderBlock() ("audio thread"). Call it from exactly one thread.
 *  - All other methods are thread-safe and may be called from a UI thread.
 *
 *  Time
 *  ----
 *  - realtime == true : the firmware thread runs freely and its delays sleep on
 *    the wall clock; System::GetNow() follows the audio sample clock, which a
 *    sound card keeps in step with real time. Use this for interactive
 *    front-ends.
 *  - realtime == false: time is the audio sample clock and the firmware thread is
 *    run in lockstep with RenderBlock(). Fully deterministic, runs as fast as the
 *    host can render. Use this for scripted / headless runs and tests.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chompi_sim
{

constexpr int kSampleRate   = 48000;
constexpr int kBlockSize    = 24; /**< samples per audio callback, as on hardware */
constexpr int kNumInputs    = 4;  /**< 0 mic, 1 unused, 2 aux L, 3 aux R */
constexpr int kNumOutputs   = 4;  /**< 0 hp L, 1 hp R, 2 line L, 3 line R */
constexpr int kNumKeyLeds   = 25; /**< "SMT" chain under the piano keys */
constexpr int kNumPanelLeds = 10; /**< "PTH" chain: knob rings and indicators */
constexpr int kNumEncoders  = 6;  /**< physical encoders SW1..SW6 */

/** The 40 inputs of the CD4021 button chain, numbered exactly like
 *  chompi::Hardware::SwId in the firmware. */
enum Button : int
{
    ENC_1_SW = 0, ENC_2_SW, ENC_3_SW, ENC_4_SW, NC_6, KEY_26, SW_TOG, KEY_16,
    KEY_2, KEY_3, KEY_4, KEY_5, KEY_17, KEY_18, KEY_19, KEY_1,
    KEY_6, KEY_7, KEY_8, KEY_9, KEY_10, KEY_20, KEY_21, KEY_22,
    KEY_11, KEY_12, KEY_13, KEY_14, KEY_15, KEY_23, KEY_24, KEY_25,
    ENC_6_SW, KEY_27, KEY_28, NC_1, NC_2, NC_3, NC_4, NC_5,
    NUM_BUTTONS
};

/** Friendly aliases for the three function keys. */
constexpr Button KEY_CHOMPI = KEY_26;
constexpr Button KEY_PLAY   = KEY_27;
constexpr Button KEY_LOOP   = KEY_28;

/** Piano keys in chromatic order, semitone 0 = leftmost C, 24 = rightmost C.
 *  White keys are KEY_1..KEY_15, black keys KEY_16..KEY_25. */
constexpr Button kPianoKeys[25] = {
    KEY_1,  KEY_16, KEY_2,  KEY_17, KEY_3,  KEY_4,  KEY_18, KEY_5,  KEY_19,
    KEY_6,  KEY_20, KEY_7,  KEY_8,  KEY_21, KEY_9,  KEY_22, KEY_10, KEY_11,
    KEY_23, KEY_12, KEY_24, KEY_13, KEY_25, KEY_14, KEY_15};

/** Index into the key LED chain for each piano semitone (firmware led_map). */
constexpr int kPianoKeyLed[25] = {24, 0, 23, 1, 22, 21, 2, 20, 3, 19, 4, 18, 17,
                                  5,  16, 6, 15, 14, 7, 13, 8, 12, 9, 11, 10};

/** Which panel LED (PTH chain index) belongs to what. Chain index i is the
 *  board's LED(i+1); positions come from the Rev4 board file. */
enum PanelLed : int
{
    PANEL_LED_CHOMPI_KEY  = 0, /**< 8 mm LED above the CHOMPI key (also lit for transport knob pushes) */
    PANEL_LED_PITCH_KNOB  = 1, /**< 8 mm LED above physical SW4 (logical knob 0) */
    PANEL_LED_KNOB_A      = 2, /**< 8 mm LED above physical SW1 (logical knob 1) */
    PANEL_LED_KNOB_B      = 3, /**< 8 mm LED above physical SW2 (logical knob 2) */
    PANEL_LED_KNOB_C      = 4, /**< 8 mm LED above physical SW3 (logical knob 3) */
    PANEL_LED_INDICATOR_A = 5, /**< 5 mm LED left of the transport knob: tempo / clock division */
    PANEL_LED_INDICATOR_B = 6, /**< 5 mm LED right of the transport knob: tempo / clock division */
    PANEL_LED_PLAY        = 7, /**< 5 mm LED above the PLAY key */
    PANEL_LED_LOOP        = 8, /**< 5 mm LED above the LOOP key */
    PANEL_LED_VOLUME_KNOB = 9, /**< 8 mm LED above physical SW6 (logical knob 5) */
    PANEL_LED_TRANSPORT_KNOB = PANEL_LED_CHOMPI_KEY, /**< old name, kept for compatibility */
};

/** Physical encoder indices (chompi::Hardware::EncoderId). */
enum Encoder : int
{
    ENC_SW1 = 0, /**< small knob, logical 1 */
    ENC_SW2 = 1, /**< small knob, logical 2 */
    ENC_SW3 = 2, /**< small knob, logical 3 */
    ENC_SW4 = 3, /**< pitch knob, logical 0 */
    ENC_SW5 = 4, /**< big transport knob, logical 4 (push button on a GPIO, not the chain) */
    ENC_SW6 = 5, /**< volume knob, logical 5 */
};

struct Rgb
{
    uint8_t r = 0, g = 0, b = 0;
};

struct Config
{
    /** Folder that plays the role of the microSD card (firmware .bin is ignored). */
    std::string card_dir;
    /** See the threading / time notes above. */
    bool realtime = true;
    /** Echo firmware PrintLine() output and simulator notices to stderr. */
    bool verbose = true;
};

/** Debug counters a front-end may want to display. */
struct Stats
{
    uint64_t blocks_rendered = 0;
    uint64_t midi_bytes_out  = 0;
    uint32_t led_frames      = 0; /**< completed key-LED DMA transfers */
    double   max_block_us    = 0; /**< worst RenderBlock() wall time */
};

/** State of the audio-input feed (see Sim::LoadInputFile). */
struct InputState
{
    std::string name;               /**< file name of the loaded clip, empty if none */
    bool        playing    = false;
    bool        loop       = false;
    double      position_s = 0;     /**< play position in the clip */
    double      length_s   = 0;
    bool        line_in    = false; /**< the aux jack is plugged */
};

class Sim
{
  public:
    static Sim& Get();

    /** Prepares the virtual device. Returns false if card_dir does not exist. */
    bool Init(const Config& cfg);
    /** Starts the firmware thread (runs the firmware's main()). */
    void Start();
    /** Asks the firmware loop to exit and joins the thread. Safe to call twice. */
    void Stop();
    bool FirmwareRunning() const;
    const Config& GetConfig() const;

    /** Advance the device by one audio block. `in` may be nullptr (silent
     *  inputs). Both arrays are indexed [channel][sample] with kBlockSize samples.
     *  Fires due timers and DMA completions, then the firmware audio callback. */
    void RenderBlock(const float* const* in, float* const* out);

    /** Convenience for sound cards: renders `frames` interleaved stereo frames of
     *  the chosen output pair (0 = headphones, 1 = line out), handling blocks
     *  that do not divide `frames`. */
    void RenderStereo(float* interleaved, size_t frames, int pair = 1);

    /** Runs RenderBlock() on an internal thread at wall-clock pace into a null
     *  sink. For realtime front-ends without a sound card. */
    void StartNullAudio();
    void StopNullAudio();

    // ---- controls (thread-safe) ----
    void SetButton(int button, bool pressed);            /**< Button enum */
    void SetToggle(bool down);                           /**< mode switch, SW_TOG */
    void TurnEncoder(int encoder, int detents);          /**< positive = clockwise */
    void SetEncoderPressed(int encoder, bool pressed);   /**< push switch of an encoder */
    bool ButtonPressed(int button) const;
    bool ToggleDown() const;

    // ---- MIDI ----
    void MidiIn(const uint8_t* bytes, size_t n, bool usb = false);
    /** Bytes the firmware sent to the TRS MIDI output since the last call. */
    std::vector<uint8_t> TakeMidiOut();
    /** Bytes the firmware sent to the USB MIDI output since the last call. */
    std::vector<uint8_t> TakeUsbMidiOut();

    // ---- LEDs (already rescaled to display brightness) ----
    Rgb KeyLed(int chainIndex) const;    /**< 0..kNumKeyLeds-1, see kPianoKeyLed */
    Rgb PanelLed(int chainIndex) const;  /**< 0..kNumPanelLeds-1, see PanelLed */

    // ---- power model (MP2722 charger) ----
    void SetUsbPower(bool plugged);
    void SetBatteryLow(bool low);
    /** Pull the card out (or put it back). The firmware shows its no-card page. */
    void SetCardPresent(bool present);

    // ---- audio inputs (thread-safe) ----
    /** Loads a WAV file (PCM or float, any rate, mono or stereo; resampled to
     *  48 kHz) as the input clip. Returns false and sets `err` on failure. */
    bool LoadInputFile(const std::string& path, std::string& err);
    /** Plays the loaded clip from its start into the microphone input (mono
     *  mix) and the aux input (stereo), which is where the firmware records
     *  from. */
    void PlayInput(bool loop = false, float gain = 1.f);
    void StopInput();
    InputState GetInputState() const;
    /** Plug or unplug the aux jack. Plugged, the firmware records and
     *  monitors the aux input instead of the microphone. */
    void SetLineIn(bool plugged);
    bool LineIn() const;
    /** Hands the firmware frames from a host microphone (mono, 48 kHz); the
     *  following blocks mix them into the inputs. */
    void PushInput(const float* mono, size_t frames);
    /** Peak level that entered the inputs since the last call (0..1), for a meter. */
    float TakeInputPeak();

    // ---- misc ----
    uint64_t SampleClock() const; /**< samples rendered so far */
    uint32_t NowMs() const;       /**< what the firmware sees as System::GetNow() */
    Stats    GetStats() const;
    /** Lines the firmware printed with PrintLine(), newest last; cleared on read. */
    std::vector<std::string> TakeLog();

  private:
    Sim()  = default;
    ~Sim() = default;
};

/** Human readable name of a Button value (e.g. "KEY_27 (PLAY)"). */
const char* ButtonName(int button);

} // namespace chompi_sim
