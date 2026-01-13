/*
    SAEngine - Simple Audio Engine
    Single-header library.
    https://github.com/eteriaal/SAEngine

    MIT License
    Copyright (c) 2026 Eteriaal

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

*/

/*
    Usage:
        In exactly ONE .cpp file:
            #define SAE_IMPLEMENTATION
            #include "SAEngine.h"

        In other .cpp files:
            #include "SAEngine.h"


    Quick start:
        // main.cpp
        #define SAE_IMPLEMENTATION
        #include "SAEngine.h"

        int main()
        {
            AudioEngine engine(8192, 4, SampleRate::Hz48000);
            auto source = engine.createSource();
            
            if (!source->load("music.ogg")) {
                std::cerr << "Failed to load file\n";
                return 1;
            }

            source->setVolume(0.7f);
            source->play();

            std::atomic<bool> running{true};
            std::thread worker([&]() {
                while (running) {
                    engine.update();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            });

            std::this_thread::sleep_for(std::chrono::seconds(1));
            while (source->isPlaying()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            
            running = false;
            worker.join();

            return 0;
        }
*/

/*
    TO-DO:
    !! fix source->setLoop(true) - restarting playback every second;
    !! fix stop() in AudioSource::load() before loading file - stuck loading.
    AL_SOFT_source_spatialize (panorama for stereo and mono);
    !!! WorkerThread + Async decoding thread (std::deque<ALuint> or ring-buffer);
    !!! Improve seek
    !! Tests (functional, memory leaks - idk if there are any);    
    loadtoram(), playfromram() or just play() - full file in ram, without streaming.

    #define SAE_DEBUG_LOG - already exists - Improve logging. 
    #define SAE_CACHING - optional async caching to raw pcm or cache;
    #define SAE_METADATA;
    #define SAE_EXTRA - maybe extra functional like: Audio transcoding/conversion \
                            probably in another header, not #define.
    #define SAE_FORCE_MONO_DOWNMIX  - Force downmix to mono (helps with pan on stereo files)
*/

#pragma once
#ifndef SAE_H
#define SAE_H

#if __cplusplus < 202002L
    #error "SAEngine requires C++20 or later"
#endif

#define SAE_VERSION "2026-01-13"
#define SAE_VERSION_STR "SAEngine single-header v" SAE_VERSION " (2026 Eteriaal)"


// Configurable macros (define before #include "SAEngine.h")
/*
    #define SAE_DEBUG_LOG - some logs, will be improved in future.
    #define NDEBUG - remove checkALError & checkALError
*/

#ifdef NDEBUG
    #define SAE_SILENT_ERRORS
#endif

#include <string>
#include <memory>
#include <vector>

// Forward declarations

enum class SampleRate : int {
    Hz44100 = 44100,
    Hz48000 = 48000
};

class AudioSource; // forward

// AudioEngine — public interface only
/*
    class AudioEngine: Manages OpenAL context, master volume, multiple audio sources.
    Thread safe for multiple sources, but controls must be from 'AudioEngine' creation thread
*/
class AudioEngine {
public:
    explicit AudioEngine(
        size_t bufferSamples = 8192,                        // Size of each streaming buffer in samples.
        int numBuffers = 4,                                 // Number of buffers per source
        SampleRate engineSampleRate = SampleRate::Hz48000   // Desired OpenAL output rate (can be 0 for default)
    );
    ~AudioEngine() noexcept;
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    void update(); // Manual update, if workerthread is not used. 
    /* Call this regularly to refill audio buffers of all active sources.
       If you don't use a worker thread — call it in your main loop.
       Example:

        std::atomic<bool> running{true};
        std::thread worker([&]() {
            while (running) {
                engine.update();

                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });

        // Later: running = false; worker.join();
    */

    // Global volume multiplier for all sources (0.0f = silent; 1.0f = full)
    void setMasterVolume(float volume); // 0.0f - 1.0f
    float masterVolume() const; // getter

    // Control all sources at once
    void pauseAll();    // Pause every playing source
    void resumeAll();   // Resume all paused sources
    void stopAll();     // Stop and clear all sources. (Clear, but not delete.)

    // Creates a new independent audio source
    std::unique_ptr<AudioSource> createSource();

private:
    friend class AudioSource;

    struct Impl;
    std::unique_ptr<Impl> impl;
};


// AudioSource — public interface only
/*
    class AudioSource: independent audio source. 
    Features: streaming decoding, seeking, volume / panning / pitch / looping, current playback position and duration queries
    Lifetime: lives as long as the unique_ptr returned from engine.createSource() lives
    Automatically registers / unregisters itself in the AudioEngine
*/
class AudioSource {
public:
    AudioSource() = delete;
    explicit AudioSource(AudioEngine* engine);
    ~AudioSource() noexcept;
    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;

    bool load(const std::string& filepath); // File loading
    /* 
        Loads an audio file and prepares it for playback.
        Supports virtually all formats that ffmpeg/libav can handle.

        Should be called before the first play().
        Can be called again to change the track on the same source. (source must be stopped before loading)

        Returns true if file was successfully opened and decoder initialized.
    */

    // Playback controls
    void play(); // Starts / resumes playback
    void pause(); // Pauses playback (keep current position)
    void stop(); // Stops playback (reset position to beginning)

    void seek(double seconds); // Seeks to the specified time position (in seconds, can be fractional).
    /*
        Works both during playback and when paused/stopped.
        When playing — automatically refills buffers after seeking.
    */

    // Playback parametres
    void setVolume(float volume);
    void setPan(float pan); // panorama
    void setPitch(float pitch);
    void setLoop(bool enabled);

    // Status and info
    bool isPlaying() const;
    bool isPaused() const;
    bool isStopped() const;
    double position() const;
    double duration() const;
    float volume() const;
    bool loop() const;
    const std::string& currentFile() const;

private:
    friend class AudioEngine;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Implementation

#ifdef SAE_IMPLEMENTATION

#ifndef SAE_IMPLEMENTATION_PROVIDED

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <span>
#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
#include <sstream>
#include <iomanip>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

#include <AL/al.h>
#include <AL/alc.h>


#ifdef SAE_SILENT_ERRORS
    #define checkALError(op)   ((void)0)
    #define checkALCError(d,o) ((void)0)
#else
static void checkALError(const std::string& operation) {
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "OpenAL error during " << operation << ": " << alGetString(error) << std::endl;
    }
}

static void checkALCError(ALCdevice* device, const std::string& operation) {
    ALCenum error = alcGetError(device);
    if (error != ALC_NO_ERROR) {
        std::cerr << "ALC error during " << operation << ": " << alcGetString(device, error) << std::endl;
    }
}
#endif

// ----------------------
// AudioEngine::Impl

struct AudioEngine::Impl {
    static constexpr std::array<SampleRate, 2> supportedRates = {
        SampleRate::Hz44100,
        SampleRate::Hz48000
    };

    const size_t bufferSamples;
    const int numBuffers;
    const int engineSampleRate;

    ALCdevice* device{nullptr};
    ALCcontext* context{nullptr};

    std::atomic<float> masterVolumeValue{1.0f};
    std::atomic<bool> globalPaused{false};

    std::mutex sourcesMutex;
    std::vector<AudioSource*> activeSources;
    std::condition_variable updateCv;

    Impl(size_t bs, int nb, SampleRate sr);
    ~Impl() noexcept;

    void registerSource(AudioSource* source);
    void unregisterSource(AudioSource* source);

    void update();
    void setMasterVolume(float volume);
    float masterVolume() const;
    void pauseAll();
    void resumeAll();
    void stopAll();
};

AudioEngine::Impl::Impl(size_t bs, int nb, SampleRate sr)
    : bufferSamples(bs),
      numBuffers(nb),
      engineSampleRate(static_cast<int>(sr)) {

    if (engineSampleRate != 0 && std::find(supportedRates.begin(), supportedRates.end(), sr) == supportedRates.end()) {
        throw std::runtime_error("Unsupported sample rate");
    }

    device = alcOpenDevice(nullptr);
    if (!device) throw std::runtime_error("Failed to open ALC device");
    checkALCError(device, "alcOpenDevice");

    ALCint attrs[] = { ALC_FREQUENCY, engineSampleRate, 0 };
    context = alcCreateContext(device, engineSampleRate != 0 ? attrs : nullptr);
    if (!context) {
        alcCloseDevice(device);
        throw std::runtime_error("Failed to create ALC context");
    }
    checkALCError(device, "alcCreateContext");

    if (!alcMakeContextCurrent(context)) {
        alcDestroyContext(context);
        alcCloseDevice(device);
        throw std::runtime_error("Failed to make ALC context current");
    }
    checkALCError(device, "alcMakeContextCurrent");

#ifdef SAE_DEBUG_LOG
    std::cout << "OpenAL Vendor:   " << alGetString(AL_VENDOR)    << std::endl;
    std::cout << "OpenAL Renderer: " << alGetString(AL_RENDERER)  << std::endl;
    std::cout << "OpenAL Version:  " << alGetString(AL_VERSION)   << std::endl;
    std::cout << "OpenAL Extensions: " << alGetString(AL_EXTENSIONS) << std::endl;

    if (alIsExtensionPresent("AL_SOFT_source_spatialize"))
        std::cout << "AL_SOFT_source_spatialize supported" << std::endl;
    else
        std::cout << "AL_SOFT_source_spatialize not supported" << std::endl;

    std::cerr << "[AudioEngine] Initialized with sample rate: "
              << std::to_string(engineSampleRate) + " Hz; buffer samples: " << std::to_string(bs) 
              << "; num buffers: " << std::to_string(nb) << "\n"; 
#endif
}

AudioEngine::Impl::~Impl() noexcept {
    {
        std::lock_guard<std::mutex> lock(sourcesMutex);
        activeSources.clear();
    }

    alcMakeContextCurrent(nullptr);
    alcDestroyContext(context);
    alcCloseDevice(device);
}

void AudioEngine::Impl::registerSource(AudioSource* src) {
    std::lock_guard<std::mutex> lk(sourcesMutex);
    activeSources.push_back(src);
}

void AudioEngine::Impl::unregisterSource(AudioSource* src) {
    std::lock_guard<std::mutex> lk(sourcesMutex);
    auto it = std::find(activeSources.begin(), activeSources.end(), src);
    if (it != activeSources.end()) activeSources.erase(it);
}

void AudioEngine::Impl::setMasterVolume(float volume) {
    masterVolumeValue = std::clamp(volume, 0.0f, 1.0f);
}

float AudioEngine::Impl::masterVolume() const {
    return masterVolumeValue.load();
}

void AudioEngine::Impl::pauseAll() {
    globalPaused = true;
    std::lock_guard<std::mutex> lk(sourcesMutex);
    for (auto* s : activeSources) if (s->isPlaying()) s->pause();
}

void AudioEngine::Impl::resumeAll() {
    globalPaused = false;
    std::lock_guard<std::mutex> lk(sourcesMutex);
    for (auto* s : activeSources) if (s->isPaused()) s->play();
}

void AudioEngine::Impl::stopAll() {
    std::lock_guard<std::mutex> lk(sourcesMutex);
    for (auto* s : activeSources) s->stop();
}

// ----------------------
// AudioSource::Impl

struct AudioSource::Impl {
    AudioSource* const owner;
    AudioEngine* const engine;

    ALuint alSource{0};
    std::vector<ALuint> buffers;

    AVFormatContext* fmt{nullptr};
    AVCodecContext* codec{nullptr};
    SwrContext* swr{nullptr};
    int streamIdx{-1};

    int outputChannels{2};
    int outputSampleRate{0};
    ALenum outputFormat{AL_FORMAT_STEREO16};

    std::vector<int16_t> decodeBuffer;

    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> loop{false};
    std::atomic<float> volume{1.0f};
    std::atomic<float> pan{0.0f};
    std::atomic<float> pitch{1.0f};

    std::atomic<int64_t> playedSamples{0};
    int64_t totalSamples{0};

    std::mutex stateMutex;
    std::string currentFile;

    Impl(AudioSource* owner, AudioEngine* engine);
    ~Impl() noexcept;

    bool openFile(const std::string& path);
    int decodeNextBlock(std::span<int16_t> out, int maxSamples);
    void updateOpenALParams();
    void refillBuffers();
    void clearBuffers();
};

AudioSource::Impl::Impl(AudioSource* o, AudioEngine* e)
    : owner(o), engine(e) {
    alGenSources(1, &alSource);
    checkALError("alGenSources");

    buffers.resize(engine->impl->numBuffers);
    alGenBuffers(static_cast<ALsizei>(buffers.size()), buffers.data());
    checkALError("alGenBuffers");

    decodeBuffer.resize(engine->impl->bufferSamples * 2);

    // registration moved to AudioEngine::createSource
}

AudioSource::Impl::~Impl() noexcept {
    alSourceStop(alSource);
    clearBuffers();

    alDeleteSources(1, &alSource);
    alDeleteBuffers(static_cast<ALsizei>(buffers.size()), buffers.data());

    if (swr)   swr_free(&swr);
    if (codec) avcodec_free_context(&codec);
    if (fmt)   avformat_close_input(&fmt);
}

bool AudioSource::Impl::openFile(const std::string& path) {
    if (fmt) {
        avformat_close_input(&fmt);
        fmt = nullptr;
    }
    codec = nullptr;
    swr = nullptr;
    streamIdx = -1;
    playedSamples = 0;
    totalSamples = 0;

    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) return false;

    streamIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIdx < 0) return false;

    AVStream* stream = fmt->streams[streamIdx];
    const AVCodec* dec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!dec) return false;

    codec = avcodec_alloc_context3(dec);
    if (avcodec_parameters_to_context(codec, stream->codecpar) < 0) return false;
    if (avcodec_open2(codec, dec, nullptr) < 0) return false;

    outputChannels = (codec->ch_layout.nb_channels > 1) ? 2 : 1;
    outputFormat = (outputChannels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

    int targetRate = engine->impl->engineSampleRate;
    outputSampleRate = (targetRate != 0) ? targetRate : codec->sample_rate;

    swr = swr_alloc();
    if (!swr) return false;

    AVChannelLayout outLayout{};
    av_channel_layout_default(&outLayout, outputChannels);

    int ret = swr_alloc_set_opts2(&swr,
        &outLayout, AV_SAMPLE_FMT_S16, outputSampleRate,
        &codec->ch_layout, codec->sample_fmt, codec->sample_rate,
        0, nullptr);

    if (ret < 0 || swr_init(swr) < 0) {
        swr_free(&swr);
        swr = nullptr;
        return false;
    }

    if (stream->duration != AV_NOPTS_VALUE) {
        double sec = stream->duration * av_q2d(stream->time_base);
        totalSamples = static_cast<int64_t>(sec * outputSampleRate);
    } else {
        totalSamples = -1;
    }

    currentFile = path;
    return true;
}

int AudioSource::Impl::decodeNextBlock(std::span<int16_t> outBuffer, int maxSamples) {
    if (!fmt || !codec || !swr) return 0;

    int total = 0;

        AVPacket* packet = av_packet_alloc();
    if (!packet) return 0;

    auto frame_deleter = [](AVFrame* ptr) {
        if (ptr) av_frame_free(&ptr);
    };

    auto frame = std::unique_ptr<AVFrame, decltype(frame_deleter)>(
        av_frame_alloc(),
        frame_deleter
    );

    if (!frame) {
        av_packet_free(&packet);
        return 0;
    }

    while (total < maxSamples) {
        int ret = av_read_frame(fmt, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF && loop) {
                owner->seek(0.0);
                continue;
            }
            break;
        }

        if (packet->stream_index != streamIdx) {
            av_packet_unref(packet);
            continue;
        }

        ret = avcodec_send_packet(codec, packet);
        av_packet_unref(packet);
        if (ret < 0) break;

        while (total < maxSamples) {
            ret = avcodec_receive_frame(codec, frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            int needed = maxSamples - total;
            int avail = swr_get_out_samples(swr, frame->nb_samples);
            int want = std::min(avail, needed);

            uint8_t* outPtr[1] = { reinterpret_cast<uint8_t*>(outBuffer.data() + total * outputChannels) };

            int converted = swr_convert(swr, outPtr, want,
                                        (const uint8_t**)frame->data, frame->nb_samples);

            if (converted < 0) break;

            total += converted;
            playedSamples += converted;

            if (converted == 0) break;
        }
    }

    av_packet_free(&packet);
    return total;
}

void AudioSource::Impl::updateOpenALParams() {
    float gain = volume * engine->impl->masterVolumeValue.load(std::memory_order_relaxed);
    alSourcef(alSource, AL_GAIN, gain);
    checkALError("alSourcef AL_GAIN");

    alSourcef(alSource, AL_PITCH, pitch);
    checkALError("alSourcef AL_PITCH");

    alSource3f(alSource, AL_POSITION, pan, 0.0f, 0.0f);
    checkALError("alSource3f AL_POSITION");
}

void AudioSource::Impl::refillBuffers() {
    ALint processed;
    alGetSourcei(alSource, AL_BUFFERS_PROCESSED, &processed);
    checkALError("alGetSourcei AL_BUFFERS_PROCESSED");

    while (processed > 0) {
        ALuint buf;
        alSourceUnqueueBuffers(alSource, 1, &buf);
        checkALError("alSourceUnqueueBuffers");

        int n = decodeNextBlock(decodeBuffer, engine->impl->bufferSamples);

        if (n > 0) {
            alBufferData(buf, outputFormat, decodeBuffer.data(),
                         n * outputChannels * sizeof(int16_t), outputSampleRate);
            checkALError("alBufferData");
            alSourceQueueBuffers(alSource, 1, &buf);
            checkALError("alSourceQueueBuffers");
        } else {
            // silence / requeue to prevent stopping
            std::cerr << "[ERROR] processed buffers <= 0; UNDERRUN \n";
            std::cerr << "[INFO] position: " << owner->position() << "\n";
            alSourceQueueBuffers(alSource, 1, &buf);
            throw std::runtime_error("underrun in refillbuffers after decodenextblock");
        }

        --processed;
    }

    ALint queued;
    alGetSourcei(alSource, AL_BUFFERS_QUEUED, &queued);
    checkALError("alGetSourcei AL_BUFFERS_QUEUED");

    if (queued == 0 && playing) {
        for (size_t i = 0; i < buffers.size(); ++i) {
            int n = decodeNextBlock(decodeBuffer, engine->impl->bufferSamples);
            if (n <= 0) break;
            alBufferData(buffers[i], outputFormat, decodeBuffer.data(),
                         n * outputChannels * sizeof(int16_t), outputSampleRate);
            alSourceQueueBuffers(alSource, 1, &buffers[i]);
        }
    }
}

void AudioSource::Impl::clearBuffers() {
    ALint queued;
    alGetSourcei(alSource, AL_BUFFERS_QUEUED, &queued);
    while (queued > 0) {
        ALuint buf;
        alSourceUnqueueBuffers(alSource, 1, &buf);
        --queued;
    }
}

// ----------------------
// AudioEngine methods forwarding

AudioEngine::AudioEngine(size_t bs, int nb, SampleRate sr)
    : impl(std::make_unique<Impl>(bs, nb, sr)) {}

AudioEngine::~AudioEngine() noexcept = default;

void AudioEngine::update()                     { impl->update(); }
void AudioEngine::setMasterVolume(float v)     { impl->setMasterVolume(v); }
float AudioEngine::masterVolume() const        { return impl->masterVolume(); }
void AudioEngine::pauseAll()                   { impl->pauseAll(); }
void AudioEngine::resumeAll()                  { impl->resumeAll(); }
void AudioEngine::stopAll()                    { impl->stopAll(); }

std::unique_ptr<AudioSource> AudioEngine::createSource() {
    auto src = std::make_unique<AudioSource>(this);
    impl->registerSource(src.get());
    return src;
}

void AudioEngine::Impl::update() {
    alListenerf(AL_GAIN, masterVolumeValue.load(std::memory_order_relaxed));
    checkALError("alListenerf AL_GAIN");

    std::vector<AudioSource*> copy;
    {
        std::lock_guard<std::mutex> lk(sourcesMutex);
        copy = activeSources;
    }

    for (auto* src : copy) {
        std::lock_guard<std::mutex> lk(src->impl->stateMutex);

        if (!src->impl->playing && !src->impl->paused) continue;

        src->impl->updateOpenALParams();
        src->impl->refillBuffers();

        ALint state;
        alGetSourcei(src->impl->alSource, AL_SOURCE_STATE, &state);
        checkALError("alGetSourcei AL_SOURCE_STATE");

        if (state == AL_STOPPED && src->impl->playing && !src->impl->loop) {
            src->stop();
            continue;
        }

        if (src->impl->playing && !src->impl->paused) {
            if (state == AL_INITIAL || state == AL_STOPPED) {
                alSourcePlay(src->impl->alSource);
                checkALError("alSourcePlay");
            }
        }
    }
}

// ----------------------
// AudioSource methods forwarding

AudioSource::AudioSource(AudioEngine* engine)
    : impl(std::make_unique<Impl>(this, engine)) {
    // registration in AudioEngine::createSource
}

AudioSource::~AudioSource() noexcept {
    stop();
    // unregistration in AudioEngine::Impl destructor
}

bool AudioSource::load(const std::string& filepath) {
    std::lock_guard<std::mutex> lk(impl->stateMutex);
    // stop(); <- stuck, why?
    if (!impl->openFile(filepath)) return false;

    // initial fill
    for (size_t i = 0; i < impl->buffers.size(); ++i) {
        int n = impl->decodeNextBlock(impl->decodeBuffer, impl->engine->impl->bufferSamples);
        if (n <= 0) break;
        alBufferData(impl->buffers[i], impl->outputFormat, impl->decodeBuffer.data(),
                     n * impl->outputChannels * sizeof(int16_t), impl->outputSampleRate);
    }
    alSourceQueueBuffers(impl->alSource, static_cast<ALsizei>(impl->buffers.size()), impl->buffers.data());
    return true;
}

void AudioSource::play() {
    std::lock_guard<std::mutex> lk(impl->stateMutex);
    if (impl->engine->impl->globalPaused) return;
    if (!impl->playing && impl->fmt) {
        impl->playing = true;
        impl->paused = false;
        impl->refillBuffers();
        alSourcePlay(impl->alSource);
    } else if (impl->paused) {
        impl->paused = false;
        alSourcePlay(impl->alSource);
    }
}

void AudioSource::pause() {
    std::lock_guard<std::mutex> lk(impl->stateMutex);
    if (impl->playing) {
        alSourcePause(impl->alSource);
        impl->paused = true;
        impl->playing = false;
    }
}

void AudioSource::stop() {
    std::lock_guard<std::mutex> lk(impl->stateMutex);
    if (impl->playing || impl->paused) {
        alSourceStop(impl->alSource);
        impl->clearBuffers();
        impl->playing = false;
        impl->paused = false;
        impl->playedSamples = 0;
    }
}

void AudioSource::seek(double seconds) {
    std::lock_guard<std::mutex> lk(impl->stateMutex);
    if (!impl->fmt || !impl->codec) return;

    int64_t ts = static_cast<int64_t>(seconds * AV_TIME_BASE);
    if (av_seek_frame(impl->fmt, impl->streamIdx, ts, AVSEEK_FLAG_BACKWARD) < 0) return;

    avcodec_flush_buffers(impl->codec);
    impl->playedSamples = static_cast<int64_t>(seconds * impl->outputSampleRate);

    if (impl->playing) {
        impl->clearBuffers();
        impl->refillBuffers();
    }
}

void AudioSource::setVolume(float v) { impl->volume = std::clamp(v, 0.0f, 1.0f); }
void AudioSource::setPan(float p)    { impl->pan    = std::clamp(p, -1.0f, 1.0f); }
void AudioSource::setPitch(float p)  { impl->pitch  = std::max(p, 0.1f); alSourcef(impl->alSource, AL_PITCH, impl->pitch); }
void AudioSource::setLoop(bool e)    { impl->loop = e; alSourcei(impl->alSource, AL_LOOPING, e ? AL_TRUE : AL_FALSE); }

bool AudioSource::isPlaying() const { return impl->playing; }
bool AudioSource::isPaused()  const { return impl->paused; }
bool AudioSource::isStopped() const { return !impl->playing && !impl->paused; }

double AudioSource::position() const { return static_cast<double>(impl->playedSamples.load()) / impl->codec->sample_rate; }
double AudioSource::duration() const { return impl->totalSamples > 0 ? static_cast<double>(impl->totalSamples) / impl->codec->sample_rate : -1.0; }
float  AudioSource::volume()   const { return impl->volume; }
bool   AudioSource::loop()     const { return impl->loop; }
const std::string& AudioSource::currentFile() const { return impl->currentFile; }

#else
    #error "SAEngine implementation has already been provided in another translation unit! \
                Make sure #define SAE_IMPLEMENTATION is only in ONE .cpp file. \
                See Usage comment at the top of SAEngine.h"
#endif

#define SAE_IMPLEMENTATION_PROVIDED

#endif // SAE_IMPLEMENTATION
#endif // SAE_H