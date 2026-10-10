/** @file GrainEngine.h
 *  @brief GRAIN: a granular sampler engine for CHOMPI, grown from WAVE.
 *
 *  Sounds live in SDRAM as 48 kHz stereo int16, up to ten seconds each: up to
 *  fourteen loaded from the card at boot (the first .wav files in name order)
 *  plus one recorded from the microphone or aux input with the CHOMPI key.
 *  Each voice plays a cloud of grains from the selected sound at the key's
 *  pitch; the filter, amp envelope, LFOs, delay, reverb and compressor are
 *  WAVE's, as is the key handling around the engine (KeyRequest).
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"
#include "fatfs.h"
#include "FileStreamingManager.h"
#include "DJFilter.h"
#include "EnvFollower.h"
#include "limiter.h"
#include "reverb.h"
#include "InterpolatedDelayLine.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cmath>

#define NUM_VOICES 8

static constexpr size_t kMaxDelayTime = 48128 * 2; // stereo, > 1 seconds at 48kHz

using namespace daisy;
using namespace daisysp;

// This shouldn't live here, but it's visible where it's needed, so...
static const uint8_t kSlotNone = 100;

struct KeyRequest
{
    enum class Type
    {
        START,
        STOP,
        DUMMY,
    };

    enum class Source
    {
        USER,
        SEQUENCER,
    };

    Type type_;
    float transpose_nn_;
    int key_;
    float vel_;
    Source src_;

    KeyRequest(Type type, float transpose_nn, int key, float vel, Source src = Source::USER)
        : type_(type), transpose_nn_(transpose_nn), key_(key), vel_(vel), src_(src)
    {
    }

    KeyRequest() : type_(Type::DUMMY), transpose_nn_(0.f), key_(0), vel_(127.f), src_(Source::USER) {}
};

namespace grain
{
    constexpr int      kNumSounds      = 15;             /**< 14 from the card + the recording */
    constexpr int      kRecordSound    = 14;             /**< the sound the CHOMPI key records into */
    constexpr uint32_t kSoundFrames    = 480000;         /**< 10 s at 48 kHz */
    constexpr uint32_t kSoundSamples   = kSoundFrames * 2; /**< stereo int16 */
    constexpr int      kGrainsPerVoice = 12;
    constexpr int      kWindowSize     = 1024;
    constexpr float    kRootHz         = 130.8128f;      /**< the middle key plays a sound at its own speed */
    constexpr float    kMicGain        = 4.f;
    constexpr float    kLineGain       = 2.f;
    constexpr float    kSprayRangeS    = 1.f;            /**< full spray = +/- one second around the playhead */
    constexpr float    kS16ToFloat     = 1.f / 32768.f;

    /** One sound in SDRAM. */
    struct Sound
    {
        int16_t* data   = nullptr; /**< interleaved stereo */
        uint32_t frames = 0;
        bool     loaded = false;
    };

    class SoundBank
    {
    public:
        void Init(int16_t* memory)
        {
            for(int i = 0; i < kNumSounds; i++)
            {
                sounds[i].data   = memory + size_t(i) * kSoundSamples;
                sounds[i].frames = 0;
                sounds[i].loaded = false;
            }
        }
        int numLoaded() const
        {
            int n = 0;
            for(int i = 0; i < kNumSounds; i++)
                n += sounds[i].loaded ? 1 : 0;
            return n;
        }
        Sound sounds[kNumSounds];
    };

    /** What the loader needs from a WAV header. */
    struct WavInfo
    {
        bool     ok          = false;
        uint16_t channels    = 0;
        uint16_t bits        = 0;
        uint32_t rate        = 0;
        uint32_t data_offset = 0;
        uint32_t data_bytes  = 0;
    };

    inline uint32_t ReadU32(const uint8_t* p)
    {
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    inline uint16_t ReadU16(const uint8_t* p)
    {
        return uint16_t(p[0] | p[1] << 8);
    }

    /** Parses the header of a WAV file on the card (called at boot, before
     *  audio starts). Only 48 kHz 16-bit PCM, mono or stereo, is accepted. */
    inline WavInfo ReadWavInfo(const char* name)
    {
        WavInfo info;
        FIL     f;
        if(f_open(&f, name, FA_READ) != FR_OK)
            return info;
        static uint8_t head[4096];
        UINT           br = 0;
        FRESULT        res = f_read(&f, head, sizeof head, &br);
        f_close(&f);
        if(res != FR_OK || br < 12 || memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "WAVE", 4) != 0)
            return info;
        uint16_t format = 0;
        for(uint32_t pos = 12; pos + 8 <= br;)
        {
            const uint8_t* ch  = head + pos;
            uint32_t       len = ReadU32(ch + 4);
            if(memcmp(ch, "fmt ", 4) == 0 && len >= 16 && pos + 8 + 16 <= br)
            {
                format        = ReadU16(ch + 8);
                info.channels = ReadU16(ch + 10);
                info.rate     = ReadU32(ch + 12);
                info.bits     = ReadU16(ch + 22);
                if(format == 0xFFFE && len >= 26)
                    format = ReadU16(ch + 32);
            }
            else if(memcmp(ch, "data", 4) == 0)
            {
                info.data_offset = pos + 8;
                info.data_bytes  = len;
                break;
            }
            pos += 8 + len + (len & 1);
        }
        info.ok = info.data_offset != 0 && format == 1 && info.bits == 16 && info.rate == 48000
                  && (info.channels == 1 || info.channels == 2);
        return info;
    }

    /** Loads the card's sounds into the bank through the FileStreamingManager,
     *  one request per SD tick, the way WAVE loads its wavetables. */
    class SampleLoader
    {
    public:
        void Init(SoundBank* bank)
        {
            bank_  = bank;
            cursor = 0;
            names.clear();
            scanCard();
        }

        void setFileManager(FileStreamingManager* fm) { file_manager = fm; }

        /** The first kMaxFromCard .wav files in name order (TAPE's _double copies
         *  left out), with a readable header. */
        void scanCard()
        {
            DIR     dir;
            FILINFO fno;
            if(f_opendir(&dir, ".") != FR_OK) // the current directory: the root, or GRAIN/ on a shared card
                return;
            std::vector<std::string> found;
            while(f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0)
            {
                size_t n = strlen(fno.fname);
                if(fno.fname[0] == '.' || n < 5)
                    continue;
                const char* ext = fno.fname + n - 4;
                if((ext[0] == '.') && (ext[1] == 'w' || ext[1] == 'W') && (ext[2] == 'a' || ext[2] == 'A')
                   && (ext[3] == 'v' || ext[3] == 'V') && !strstr(fno.fname, "_double"))
                    found.push_back(fno.fname); // TAPE's _double files are the same sounds an octave up
            }
            f_closedir(&dir);
            std::sort(found.begin(), found.end());
            for(const std::string& n : found)
            {
                if(names.size() >= size_t(kMaxFromCard))
                    break;
                WavInfo info = ReadWavInfo(n.c_str());
                if(info.ok)
                {
                    names.push_back(n);
                    infos.push_back(info);
                }
            }
        }

        void loadAllToMemory()
        {
            for(size_t t = 0; t < names.size(); t++)
            {
                const WavInfo& info     = infos[t];
                uint32_t       capacity = info.channels == 1 ? kSoundFrames * 2 : kSoundFrames * 4;
                uint32_t       bytes    = std::min(info.data_bytes, capacity);
                bytes -= bytes % (info.channels * 2);
                FileRequest openReq(FileRequest::Type::OPEN, &file, names[t].c_str(), 0, nullptr, this, nullptr);
                file_manager->request_fifo.PushBack(openReq);
                FileRequest seekReq(FileRequest::Type::SEEK, &file, nullptr, info.data_offset, nullptr, this, nullptr);
                file_manager->request_fifo.PushBack(seekReq);
                FileRequest readReq(FileRequest::Type::MASS_READ, &file, nullptr, bytes, nullptr, this,
                                    reinterpret_cast<float*>(bank_->sounds[t].data));
                file_manager->request_fifo.PushBack(readReq);
            }
        }

        // ---- callbacks from FileStreamingManager, in load order ----
        void markSetupResult(bool ok)
        {
            if(cursor < int(names.size()) && !ok)
                failed[cursor] = true;
        }
        bool currentLoadFailed() { return cursor < int(names.size()) && failed[cursor]; }
        void markReadResult(bool ok)
        {
            if(cursor >= int(names.size()))
                return;
            Sound& s = bank_->sounds[cursor];
            if(ok && !failed[cursor])
            {
                const WavInfo& info     = infos[cursor];
                uint32_t       capacity = info.channels == 1 ? kSoundFrames * 2 : kSoundFrames * 4;
                uint32_t       bytes    = std::min(info.data_bytes, capacity);
                uint32_t       frames   = bytes / (info.channels * 2);
                if(info.channels == 1)
                {
                    // the file landed as mono int16; spread it to stereo in place, from the end
                    for(int32_t i = int32_t(frames) - 1; i >= 0; i--)
                    {
                        int16_t v           = s.data[i];
                        s.data[2 * i]       = v;
                        s.data[2 * i + 1]   = v;
                    }
                }
                s.frames = frames;
                s.loaded = frames > 0;
            }
            else
            {
                s.frames = 0;
                s.loaded = false;
            }
            cursor++;
        }

        int numFiles() const { return int(names.size()); }

        static const int kMaxFromCard = kNumSounds - 1;

        std::vector<std::string> names;
        std::vector<WavInfo>     infos;
        bool                     failed[kNumSounds] = {};
        int                      cursor = 0;
        FIL                      file;
        SoundBank*               bank_        = nullptr;
        FileStreamingManager*    file_manager = nullptr;
    };

    /** Grain windows: a fat one, a Hann and a pointed one; the texture knob
     *  blends between neighbours. Filled once at init. */
    struct Windows
    {
        float table[3][kWindowSize];
        void Init()
        {
            for(int i = 0; i < kWindowSize; i++)
            {
                float hann  = 0.5f - 0.5f * cosf(2.f * 3.14159265f * (float(i) + 0.5f) / float(kWindowSize));
                table[0][i] = sqrtf(hann);  // fat: sine window, drones blend
                table[1][i] = hann;
                table[2][i] = hann * hann;  // pointed: percussive, grainy
            }
        }
    };

    struct Grain
    {
        bool     active = false;
        bool     reverse = false;
        float    pos    = 0.f;   /**< read position, frames */
        float    step   = 1.f;   /**< frames per sample, signed */
        uint32_t age    = 0;
        uint32_t length = 1;
        float    inv_length = 1.f;
        float    amp    = 0.f;
        float    pan_l  = 1.f, pan_r = 1.f;
    };

    /** A voice's cloud of grains over one sound. */
    class GrainCloud
    {
    public:
        void Init(float sr, const Windows* win, uint32_t seed)
        {
            sr_   = sr;
            win_  = win;
            rng_  = seed * 2654435761u + 1u;
            Reset();
            setSize(.5f);
            setDensity(.5f);
            setSpray(.15f);
            setTexture(.3f);
            setScan(.5f);
        }

        void setSound(const Sound* s) { sound_ = s; }
        void setRate(float r) { rate_ = r; }

        /** Grain length: 5 ms to 500 ms. */
        void setSize(float v) { size_s_ = .005f * powf(100.f, fclamp(v, 0.f, 1.f)); }
        /** Overlap: half a grain to eight grains at a time. */
        void setDensity(float v)
        {
            overlap_    = .5f * powf(16.f, fclamp(v, 0.f, 1.f));
            grain_gain_ = .9f / sqrtf(overlap_);
        }
        void setSpray(float v) { spray_ = fclamp(v, 0.f, 1.f); }
        /** Window shape, and above the middle a growing share of reversed grains. */
        void setTexture(float v)
        {
            v = fclamp(v, 0.f, 1.f);
            if(v < .5f)
            {
                win_a_ = 0;
                win_mix_ = v * 2.f;
            }
            else
            {
                win_a_ = 1;
                win_mix_ = (v - .5f) * 2.f;
            }
            reverse_prob_ = v > .5f ? (v - .5f) : 0.f;
        }
        /** Playhead speed: frozen below 4 %, natural at the middle, four times at the top. */
        void setScan(float v)
        {
            v         = fclamp(v, 0.f, 1.f);
            scan_rate_ = v < .04f ? 0.f : powf(2.f, (v - .5f) * 4.f);
        }
        /** Moves the playhead to a point of the sound (0..1). */
        void setPosition(float v)
        {
            position_ = fclamp(v, 0.f, 1.f);
            if(sound_ && sound_->frames)
                playhead_ = position_ * float(sound_->frames - 1);
        }

        void noteOn()
        {
            Reset();
            setPosition(position_);
            spawn_counter_ = 0; // a grain right away
        }

        void Reset()
        {
            for(Grain& g : grains_)
                g.active = false;
            spawn_counter_ = 0;
        }

        bool anyActive() const
        {
            for(const Grain& g : grains_)
                if(g.active)
                    return true;
            return false;
        }

        /** 0..1 position of the playhead in the sound. */
        float playhead() const
        {
            return sound_ && sound_->frames ? playhead_ / float(sound_->frames) : 0.f;
        }

        void Process(float* l, float* r)
        {
            const Sound* s = sound_;
            if(!s || s->frames < 64)
            {
                *l = *r = 0.f;
                return;
            }
            const uint32_t frames = s->frames;
            const float    ff     = float(frames);

            if(spawn_counter_ == 0)
                Spawn(frames);
            else
                spawn_counter_--;

            playhead_ += scan_rate_;
            while(playhead_ >= ff)
                playhead_ -= ff;

            const int16_t* data = s->data;
            const float*   wa   = win_->table[win_a_];
            const float*   wb   = win_->table[win_a_ + 1];
            float          suml = 0.f, sumr = 0.f;
            for(Grain& g : grains_)
            {
                if(!g.active)
                    continue;
                const int   wi = int(float(g.age) * g.inv_length * float(kWindowSize - 1));
                const float w  = wa[wi] + (wb[wi] - wa[wi]) * win_mix_;
                float       p  = g.pos;
                if(p < 0.f)
                    p += ff;
                else if(p >= ff)
                    p -= ff;
                const uint32_t i0   = uint32_t(p);
                const float    frac = p - float(i0);
                const uint32_t i1   = (i0 + 1 >= frames) ? 0 : i0 + 1;
                const float    sl   = float(data[i0 * 2]) + (float(data[i1 * 2]) - float(data[i0 * 2])) * frac;
                const float    sr   = float(data[i0 * 2 + 1]) + (float(data[i1 * 2 + 1]) - float(data[i0 * 2 + 1])) * frac;
                const float    a    = w * g.amp;
                suml += sl * a * g.pan_l;
                sumr += sr * a * g.pan_r;
                g.pos += g.step;
                if(++g.age >= g.length)
                    g.active = false;
            }
            *l = suml * kS16ToFloat;
            *r = sumr * kS16ToFloat;
        }

    private:
        float Rand()
        {
            rng_ ^= rng_ << 13;
            rng_ ^= rng_ >> 17;
            rng_ ^= rng_ << 5;
            return float(rng_ >> 8) * (1.f / 16777216.f);
        }

        void Spawn(uint32_t frames)
        {
            Grain* g = nullptr;
            for(Grain& c : grains_)
                if(!c.active)
                {
                    g = &c;
                    break;
                }
            float len = size_s_ * sr_;
            if(len > float(frames) * .5f)
                len = float(frames) * .5f;
            if(len < 48.f)
                len = 48.f;
            if(g)
            {
                float start = playhead_ + (Rand() * 2.f - 1.f) * spray_ * kSprayRangeS * sr_;
                const float ff = float(frames);
                while(start < 0.f)
                    start += ff;
                while(start >= ff)
                    start -= ff;
                const bool reverse = Rand() < reverse_prob_;
                const float pan    = .5f + (Rand() - .5f) * .6f;
                g->active     = true;
                g->reverse    = reverse;
                g->pos        = start;
                g->step       = reverse ? -rate_ : rate_;
                g->age        = 0;
                g->length     = uint32_t(len);
                g->inv_length = 1.f / len;
                g->amp        = grain_gain_;
                g->pan_l      = 1.f - pan;
                g->pan_r      = pan;
            }
            float interval = len / overlap_ * (.75f + .5f * Rand());
            spawn_counter_ = interval < 1.f ? 1u : uint32_t(interval);
        }

        Grain          grains_[kGrainsPerVoice];
        const Sound*   sound_ = nullptr;
        const Windows* win_   = nullptr;
        float          sr_    = 48000.f;
        uint32_t       rng_   = 1;
        float          rate_  = 1.f;
        float          size_s_ = .1f, overlap_ = 2.f, grain_gain_ = .6f, spray_ = 0.f, scan_rate_ = 1.f;
        float          position_ = 0.f, playhead_ = 0.f, reverse_prob_ = 0.f, win_mix_ = 0.f;
        int            win_a_ = 0;
        uint32_t       spawn_counter_ = 0;
    };
} // namespace grain

class grainVoice {
    public:

    grainVoice() {};
    ~grainVoice() {};

    void Init(float sample_rate, float *filter_lfo_val, float *pitch_lfo_mult, const grain::Windows* win, uint32_t idx) {

        cloud.Init(sample_rate, win, idx + 1);

        amp_env.Init(sample_rate);
        amp_env.SetSustainLevel(1.f); //This won't change

        cutoff_position = .5f;
        filter_.Init(sample_rate);
        filter_.SetControl(.5f);
        filter_.SetRes(0.f);

        filter_lfo_val_ = filter_lfo_val;
        pitch_lfo_mult_ = pitch_lfo_mult;

        activeFromUser = false;
        activeFromSequencer = false;
    }

    void Process(float *sigl, float *sigr) {
        const float env = amp_env.Process(gate);
        if (!gate && env < 1e-4f) {
            if (cloud.anyActive())
                cloud.Reset();
            return;
        }

        cloud.setRate(frequency * *pitch_lfo_mult_ / grain::kRootHz);

        float gl, gr;
        cloud.Process(&gl, &gr);
        const float a = env * velocity;

        //filter LFO value is 0 when toggled off - additive, same math as the old LFO mode
        filter_.SetControl(cutoff_position + *filter_lfo_val_);

        float templ, tempr;
        filter_.Process(gl * a, gr * a, &templ, &tempr);
        *sigl += templ;
        *sigr += tempr;
    }

    void setFrequency(float freq) {
        frequency = freq;
    }

    bool playing() { return amp_env.IsRunning(); }

    int prioroty;
    float frequency = grain::kRootHz;
    int key;
    float nn;
    bool gate;
    float cutoff_position;
    DjFilter filter_;
    Adsr amp_env;
    grain::GrainCloud cloud;
    bool activeFromUser;
    bool activeFromSequencer;
    float velocity = 1.f;

    private:
    float *filter_lfo_val_;
    float *pitch_lfo_mult_;

};

class myEngine {
    public:

    myEngine() {};
    ~myEngine() {};

    FIFO<KeyRequest, 64> request_fifo;

    grainVoice myVoices[NUM_VOICES];

    void Init(float sample_rate, InterpolatedDelayLine::AudioSample* del, daisysp::Reverb* reverb,
              grain::SoundBank* bank, grain::SampleLoader* loader) {
        windows_.Init();
        bank_ = bank;
        loader_ = loader;
        sound_ = 0;
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].Init(sample_rate, &filter_lfo_val, &pitch_lfo_mult, &windows_, uint32_t(i));
            myVoices[i].key = -1;
            myVoices[i].prioroty = i + 1;
            myVoices[i].cloud.setSound(&bank_->sounds[0]);
        }

        filterLfo.Init(sample_rate);
        filterLfo.SetWaveform(Oscillator::WAVE_TRI);
        filterLfo.SetAmp(0.f);
        pitchLfo.Init(sample_rate);
        pitchLfo.SetWaveform(Oscillator::WAVE_TRI);
        pitchLfo.SetAmp(0.f);
        pitchLfo.PhaseAdd(.25f);
        setPitchLfoDepth(.25f); // the LFOs have no depth knobs here: a fixed depth, switched in the menu
        setLfoDepth(.5f);
        setMasterResonance(.63f);
        setLfoRate(.58f);
        setPitchLfoRate(.58f);

        globalFrequency = .5f;

        del_.Init(del, kMaxDelayTime);
        del_.SetDelay(kMaxDelayTime * .5f);
        setDelayTime(.5f);
        delay_time = delay_time_target;
        reverb_time = reverb_time_target;

        dcblock_fx_l_.Init(sample_rate);
        dcblock_fx_r_.Init(sample_rate);

        reverb_ = reverb;

        octaveOffset = 0;

        reverb_->Init(sample_rate);
        reverb_->SetAmount(0.f);
        reverb_->SetInputGain(.3f);
        reverb_->SetLowpass(1.f);

        output_env_follower.Init();

        saturate_amt_ = saturate_amt_target_ = 1.f;
        saturate_makeup_ = saturate_makeup_target_ = 1.f;

        lim_hp_l_.Init();
        lim_hp_r_.Init();
        lim_line_l_.Init();
        lim_line_r_.Init();

        pan = pan_target = .5f;

        voice_slot_ = 15;

        file_manager.Init(sample_rate);
        loader_->setFileManager(&file_manager);
    }

    void Prepare() {
        ProcessKeyReqs();
    }

    void stopAllVoices() {
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].gate = false;
        }
    }

    void Process(const float *const *in, float **out, size_t size) {

        std::fill(out[0], out[0] + size, 0.f);
        std::fill(out[1], out[1] + size, 0.f);

        for (size_t i = 0; i < size; ++i) {
            filter_lfo_val = filter_lfo_on ? filterLfo.Process() : 0.f;
            pitch_lfo_mult = pitch_lfo_on
                ? powf(2.f, pitchLfo.Process() * (2.f / 12.f)) // full depth = +/- 2 semitones
                : 1.f;

            float sigl = 0.f;
            float sigr = 0.f;
            for (int voice = 0; voice < NUM_VOICES; ++voice) {
                myVoices[voice].Process(&sigl, &sigr);
            }
            out[0][i] = sigl;
            out[1][i] = sigr;
        }

        // the CHOMPI key records the input into the last sound, and you hear what goes in
        if (recording_) {
            grain::Sound& s = bank_->sounds[grain::kRecordSound];
            for (size_t i = 0; i < size; ++i) {
                float il = line_in_ ? in[2][i] * grain::kLineGain : in[0][i] * grain::kMicGain;
                float ir = line_in_ ? in[3][i] * grain::kLineGain : il;
                il = fclamp(il, -1.f, 1.f);
                ir = fclamp(ir, -1.f, 1.f);
                s.data[rec_frames_ * 2]     = int16_t(il * 32767.f);
                s.data[rec_frames_ * 2 + 1] = int16_t(ir * 32767.f);
                out[0][i] += il * .35f;
                out[1][i] += ir * .35f;
                if (++rec_frames_ >= grain::kSoundFrames) {
                    stopRecording();
                    break;
                }
            }
        }

        ApplyFX(out[0], out[1], size);

        for (size_t i = 0; i < size; ++i) {
            fonepole(final_lim_, final_lim_target_, .001f);
            fonepole(saturate_amt_, saturate_amt_target_, .001f);
            fonepole(saturate_makeup_, saturate_makeup_target_, .001f);
            fonepole(gain, gain_target, .001f);

            out[0][i] *= gain;
            out[1][i] *= gain;
            out[2][i] = out[0][i];
            out[3][i] = out[1][i];

            const float thresh = 1.f / (10.f * final_lim_ + 4.f);
            const float ratio = 1.f + final_lim_ * final_lim_ * 7.f;
            const float makeup = .9f + final_lim_ * .6f;
            const float pregain = 7.f * final_lim_ + 1.f;
            out[0][i] = lim_hp_l_.ProcessComp(out[0][i], pregain, thresh, ratio, makeup);
            out[1][i] = lim_hp_r_.ProcessComp(out[1][i], pregain, thresh, ratio, makeup);
            out[2][i] = lim_line_l_.ProcessComp(out[2][i], pregain, thresh, ratio, makeup);
            out[3][i] = lim_line_r_.ProcessComp(out[3][i], pregain, thresh, ratio, makeup);

            out[0][i] = daisysp::SoftClip(saturate_amt_ * out[0][i]);
            out[1][i] = daisysp::SoftClip(saturate_amt_ * out[1][i]);
            out[2][i] = daisysp::SoftClip(saturate_amt_ * out[2][i]);
            out[3][i] = daisysp::SoftClip(saturate_amt_ * out[3][i]);

            out[0][i] *= saturate_makeup_;
            out[1][i] *= saturate_makeup_;
            out[2][i] *= saturate_makeup_;
            out[3][i] *= saturate_makeup_;

            output_env_follower.Process((out[0][i] + out[1][i]));
        }

        for (size_t i = 0; i < size; ++i) {
            fonepole(pan, pan_target, .001f);

            out[0][i] *= (1.f - pan) * 2.f;
            out[1][i] *= pan * 2.f;

            out[2][i] = out[0][i];
            out[3][i] = out[1][i];
        }
    }

    void ApplyFX(float* outl, float* outr, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            outl[i] = dcblock_fx_l_.Process(outl[i]);
            outr[i] = dcblock_fx_r_.Process(outr[i]);
        }

        for (size_t i = 0; i < size; ++i) {
            fonepole(delay_amount, delay_amount_target, .001f);
            fonepole(delay_feedback, delay_feedback_target, .001f);
            fonepole(reverb_amount, reverb_amount_target, .001f);
            fonepole(delay_time, delay_time_target, .001f);

            del_.SetDelay(delay_time);

            float del_vol = delay_feedback < .2f ? delay_feedback * 5.f : 1.f;

            InterpolatedDelayLine::AudioSample del_read = del_.Read();

            float delsig_l = s162f(del_read.l) * del_vol;
            float delsig_r = s162f(del_read.r) * del_vol;

            float mono_sum = (outl[i] + outr[i]) * .5f;
            float del_in = mono_sum + delsig_r * powf(delay_feedback, .7f);
            InterpolatedDelayLine::AudioSample del_write = {int16_t(f2s16(del_in)), int16_t(f2s16(delsig_l))};
            del_.Write(del_write);

            float wet_mix = delay_amount > .25f ? .5f : 2.f * delay_amount;
            float dry_mix = delay_amount > .83f ? .5f : (1 - .6f * delay_amount);

            outl[i] = outl[i] * dry_mix + delsig_l * wet_mix;
            outr[i] = outr[i] * dry_mix + delsig_r * wet_mix;
        }

        for (size_t i = 0; i < size; ++i) {
            fonepole(reverb_time, reverb_time_target, .001f);
            reverb_->SetAmount(reverb_amount * reverb_amount * .8f);
            reverb_->SetTime(reverb_time);
            reverb_->SetLowpass(reverb_amount * .6f + .4f);
            reverb_->SetDiffusion(reverb_amount * .6f);

            reverb_->Process(&outl[i], &outr[i]);
        }
    }

    // ---- grain parameters, applied to every voice ----
    void setPosition(float v) { for (auto& voice : myVoices) voice.cloud.setPosition(v); }
    void setSpray(float v)    { for (auto& voice : myVoices) voice.cloud.setSpray(v); }
    void setSize(float v)     { for (auto& voice : myVoices) voice.cloud.setSize(v); }
    void setDensity(float v)  { for (auto& voice : myVoices) voice.cloud.setDensity(v); }
    void setTexture(float v)  { for (auto& voice : myVoices) voice.cloud.setTexture(v); }
    void setScan(float v)     { for (auto& voice : myVoices) voice.cloud.setScan(v); }

    /** Which sound the voices play: 0..13 from the card, 14 the recording. */
    void selectSound(int idx) {
        if (idx < 0 || idx >= grain::kNumSounds) return;
        sound_ = idx;
        for (auto& voice : myVoices) voice.cloud.setSound(&bank_->sounds[idx]);
    }
    /** The next loaded sound in that direction, staying put at the ends. */
    void nextSound(int dir) {
        int idx = sound_;
        while (true) {
            idx += dir > 0 ? 1 : -1;
            if (idx < 0 || idx >= grain::kNumSounds) return;
            if (bank_->sounds[idx].loaded) { selectSound(idx); return; }
        }
    }
    int getSound() { return sound_; }
    int numSounds() { return bank_->numLoaded(); }
    bool soundLoaded(int idx) { return idx >= 0 && idx < grain::kNumSounds && bank_->sounds[idx].loaded; }

    // ---- recording from the inputs into the last sound ----
    void startRecording() {
        grain::Sound& s = bank_->sounds[grain::kRecordSound];
        s.frames = 0;
        s.loaded = false;
        rec_frames_ = 0;
        recording_ = true;
    }
    void stopRecording() {
        if (!recording_) return;
        recording_ = false;
        grain::Sound& s = bank_->sounds[grain::kRecordSound];
        s.frames = rec_frames_;
        s.loaded = rec_frames_ > 4800;
        if (s.loaded) selectSound(grain::kRecordSound);
    }
    bool isRecording() { return recording_; }
    float recordProgress() { return float(rec_frames_) / float(grain::kSoundFrames); }
    void setLineIn(bool plugged) { line_in_ = plugged; }

    /** 0..1 playhead of a voice, for the LEDs; -1 when the voice is silent. */
    float getPlayhead(int voice) {
        if (voice < 0 || voice >= NUM_VOICES || !myVoices[voice].playing()) return -1.f;
        return myVoices[voice].cloud.playhead();
    }

    void setGlobalPitch(float amount) {
        //.5 = no change, 0.f = octave down, 1.f = octave up
        globalFrequency = amount;
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].setFrequency(keyToFrequency(myVoices[i].nn));
        }
    }

    void setOctave(int value) {
        octaveOffset += value;
        if (octaveOffset > 1) {
            octaveOffset = 1;
        }
        else if (octaveOffset < -1) {
            octaveOffset = -1;
        }
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].setFrequency(keyToFrequency(myVoices[i].nn));
        }
    }

    int getOctave() {
        return octaveOffset;
    }

    void setAttack(float amount) {
        if (amount < .008) {
            amount = .001;
        }
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].amp_env.SetAttackTime(amount * 5.f, 1.f);
        }
    }

    void setRelease(float amount) {
        if (amount < .008) {
            amount = .005;
        }
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].amp_env.SetReleaseTime(amount);
        }
    }

    void setLfoDepth(float amount) {
        filterLfo.SetAmp(amount);
    }

    void setLfoRate(float amount) {
        filterLfo.SetFreq(.14f * powf(65.41f / .14f, amount));
    }

    void setGain(float amount) {
        gain_target = amount;
    }

    void setFinalComp(float amount) {
        if (amount < .5f) {
            saturate_amt_target_ = 1.f;
            saturate_makeup_target_ = 1.f;
            final_lim_target_ = amount * 1.4f;
        }
        else {
            float val = logf(3.4f * (amount - .5f) + 1.f);
            saturate_amt_target_ = val * 100.f + 1.f;
            if (saturate_amt_target_ < 5.f) {
                saturate_makeup_target_ = .25f;
            }
            else if (saturate_amt_target_ < 15.f) {
                saturate_makeup_target_ = .13f;
            }
            else if (saturate_amt_target_ < 24.f) {
                saturate_makeup_target_ = .085f;
            }
            else if (saturate_amt_target_ < 30.f) {
                saturate_makeup_target_ = .072f;
            }
            else if (saturate_amt_target_ < 40.f) {
                saturate_makeup_target_ = .065f;
            }
            else if (saturate_amt_target_ < 52.f) {
                saturate_makeup_target_ = .058f;
            }
            else if (saturate_amt_target_ < 60.f) {
                saturate_makeup_target_ = .05f;
            }
            else if (saturate_amt_target_ < 70.f) {
                saturate_makeup_target_ = .046f;
            }
            else if (saturate_amt_target_ < 88.f) {
                saturate_makeup_target_ = .042f;
            }
            else {
                saturate_makeup_target_ = .04f;
            }
            final_lim_target_ = .7f;
        }
    }

    bool isKeyPlaying(int key) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == key && myVoices[i].amp_env.IsRunning()) {
                return true;
            }
        }
        return false;
    }

    float getVUSample() {
        return output_env_follower.GetLastSamp();
    }

    void ProcessKeyReqs() {
            if (!request_fifo.IsEmpty())
            {
                KeyRequest req = request_fifo.PopFront();

                if (req.type_ == KeyRequest::Type::START) {
                    StartPlayback(req.transpose_nn_, req.key_, req.vel_, req.src_);
                }
                else if (req.type_ == KeyRequest::Type::STOP) {
                    StopPlayback(req.key_, req.src_);
                }
            }
    }

    void setPitchLfoOn(bool on) { pitch_lfo_on = on; }
    void setFilterLfoOn(bool on) { filter_lfo_on = on; }
    bool getPitchLfoOn() { return pitch_lfo_on; }
    bool getFilterLfoOn() { return filter_lfo_on; }

    void setPitchLfoDepth(float amount) {
        pitchLfo.SetAmp(amount);
    }

    void setPitchLfoRate(float amount) {
        pitchLfo.SetFreq(.14f * powf(65.41f / .14f, amount));
    }

    void setMasterCutoff(float amount) {
        master_cutoff = amount;
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].cutoff_position = amount;
        }
    }

    void setMasterResonance(float amount) {
        amount = fclamp(amount, 0.f, .99f);
        master_resonance = amount;
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].filter_.SetRes(amount);
        }
    }

    void setDelayFeedback(float amount) {
        if (amount < .5f) {
            amount = 1 - amount * 2.f;
            delay_feedback_target = amount * .9;
            delay_amount_target = 1.3f * logf(amount + 1.f);
            reverb_amount_target = 0.f;
        }
        else {
            amount = (amount - .5f) * 2.f;
            reverb_amount_target = 1.3f * logf(amount + 1.f);
            delay_feedback_target = 0.f;
            delay_amount_target = 0.f;
        }
    }

    void setDelayTime(float amount) {
        delay_time_target = .99f * powf(amount, 3.f) * kMaxDelayTime + 450;

        reverb_time_target = fclamp(amount, .05f, .97f);
    }

    void setPan(float amount) {
        pan_target = amount;
    }

    void setSwitchState(bool state) {
        switch_state = state;
    }

    bool getSwitchState() {
        return switch_state;
    }

    void setVoiceSlot(uint8_t slot) {
        voice_slot_ = slot;
    }

    int getVoiceSlot() {
        return voice_slot_;
    }

    bool ProcessFileRequests()
    {
        file_manager.ProcessRequests();
        return !file_manager.request_fifo.IsEmpty();
    }

    bool checkLoaded() {
        return file_manager.request_fifo.IsEmpty();
    }

    /** Picks the first loaded sound once the card is in. */
    void selectFirstLoaded() {
        for (int i = 0; i < grain::kNumSounds; ++i) {
            if (bank_->sounds[i].loaded) { selectSound(i); return; }
        }
    }

    private:

    void StartPlayback(float transpose_nn, int key, float vel, KeyRequest::Source src) {
        int idx = -1;
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == key) {
                idx = i;
                break;
            }
        }

        if (idx != -1) {
            int p;
            myVoices[idx].gate = true;
            myVoices[idx].velocity = vel * 0.00787401f;
            myVoices[idx].cloud.noteOn();
            p = myVoices[idx].prioroty;
            myVoices[idx].prioroty = 0;
            if (src == KeyRequest::Source::USER) {
                myVoices[idx].activeFromUser = true;
            }
            else {
                myVoices[idx].activeFromSequencer = true;
            }
            for (int i = 0; i < NUM_VOICES; ++i) {
                if (myVoices[i].prioroty < p) {
                    myVoices[i].prioroty++;
                }
            }
        }
        else {
            int voiceToSteal = -1;
            int oldestVoice = -1;
            int maxPriority = -1;

            for (int i = 0; i < NUM_VOICES; ++i) {
                if (!myVoices[i].gate && myVoices[i].prioroty > maxPriority) {
                    voiceToSteal = i;
                    maxPriority = myVoices[i].prioroty;
                }
                if (myVoices[i].prioroty == NUM_VOICES) {
                    oldestVoice = i;
                }
            }

            if (voiceToSteal == -1) {
                voiceToSteal = oldestVoice;
            }

            if (voiceToSteal != -1) {
                myVoices[voiceToSteal].setFrequency(keyToFrequency(transpose_nn));
                myVoices[voiceToSteal].key = key;
                myVoices[voiceToSteal].velocity = vel * 0.00787401f;
                myVoices[voiceToSteal].nn = transpose_nn;
                myVoices[voiceToSteal].gate = true;
                myVoices[voiceToSteal].prioroty = 0;
                myVoices[voiceToSteal].cloud.noteOn();
                if (src == KeyRequest::Source::USER) {
                    myVoices[voiceToSteal].activeFromUser = true;
                }
                else {
                    myVoices[voiceToSteal].activeFromSequencer = true;
                }
            }

            for (int i = 0; i < NUM_VOICES; ++i) {
                if (i != voiceToSteal) {
                    myVoices[i].prioroty++;
                }
            }
        }
    }

    void StopPlayback(int key, KeyRequest::Source src) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == key) {
                if (src == KeyRequest::Source::USER) {
                    myVoices[i].activeFromUser = false;
                }
                else {
                    myVoices[i].activeFromSequencer = false;
                }
                if (!myVoices[i].activeFromUser && !myVoices[i].activeFromSequencer) {
                    myVoices[i].gate = false;
                }
                break;
            }
        }
    }

    float keyToFrequency(int transpose_nn) {
        const float referenceFrequency = 440.f;
        const int referenceKey = 57;
        float baseFrequency = referenceFrequency * pow(2.f, ((transpose_nn + 36 - referenceKey + (octaveOffset * 12)) / 12.f));
        float octaveShift = pow(2.f, (globalFrequency - 0.5f) * 2.f);
        return baseFrequency * octaveShift;
    }

    grain::Windows windows_;
    grain::SoundBank* bank_;
    grain::SampleLoader* loader_;
    int sound_ = 0;
    bool recording_ = false;
    bool line_in_ = false;
    uint32_t rec_frames_ = 0;

    float globalFrequency;
    int octaveOffset;
    daisysp::Oscillator pitchLfo;
    float filter_lfo_val = 0.f;
    float pitch_lfo_mult = 1.f;
    bool pitch_lfo_on = false;
    bool filter_lfo_on = false;
    InterpolatedDelayLine del_;
    daisysp::Reverb *reverb_;
    EnvFollower output_env_follower;
    chompi::Limiter lim_hp_l_;
    chompi::Limiter lim_hp_r_;
    chompi::Limiter lim_line_l_;
    chompi::Limiter lim_line_r_;
    daisysp::Oscillator filterLfo;
    float final_lim_, final_lim_target_;
    bool switch_state;
    float master_cutoff;
    float master_resonance;
    float delay_feedback, delay_feedback_target;
    float delay_amount, delay_amount_target;
    float saturate_amt_, saturate_amt_target_;
    float saturate_makeup_, saturate_makeup_target_;
    float reverb_amount, reverb_amount_target;
    float reverb_time, reverb_time_target;
    float delay_time, delay_time_target;
    daisysp::DcBlock dcblock_fx_l_, dcblock_fx_r_;
    float gain, gain_target;
    float pan, pan_target;
    uint8_t voice_slot_;

    daisy::FileStreamingManager file_manager;
};
