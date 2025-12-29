// SAEngine.h from SimpleAudioEngine. https://github.com/eteriaal/SAEngine

/// IMPORTANT: Thread Safety Note
/// 
/// All control methods (play, pause, stop, seek, setVolume, setPan, setPitch, setLoop,
/// pauseAll, resumeAll, stopAll) must be called exclusively from the thread
/// where the AudioEngine was created (typically the main/UI thread, or in a special audio thread).

#pragma once

// #define SAE_DEBUG

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <condition_variable>
#include <array>
#include <memory>
#include <span>
#include <iomanip>
#include <sstream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

#include <AL/al.h>
#include <AL/alc.h>

enum class SampleRate : int {
	Hz44100 = 44100,
	Hz48000 = 48000
};

class AudioSource;

class AudioEngine {
private:
    static constexpr std::array<SampleRate, 2> supportedRates = {
		SampleRate::Hz44100,
		SampleRate::Hz48000
	};

    const size_t m_bufferSamples;
    const int m_numBuffers;
    const int m_engineSampleRate;

public:
    explicit AudioEngine(
        size_t bufferSamples = 8192,
        int numBuffers = 4,
        SampleRate engineSampleRate = SampleRate::Hz48000
    );

    ~AudioEngine() noexcept;

    void update();

    void setMasterVolume(float volume);
    float masterVolume() const;

    void pauseAll();
    void resumeAll();
    void stopAll();

    std::unique_ptr<AudioSource> createSource();

private:
    friend class AudioSource;

    void registerSource(AudioSource* source);
    void unregisterSource(AudioSource* source);

    ALCdevice* m_device{ nullptr };
    ALCcontext* m_context{ nullptr };

    std::atomic<float> m_masterVolume{ 1.0f };
    std::atomic<bool> m_globalPaused{ false };

    std::mutex m_sourcesMutex;
    std::vector<AudioSource*> m_activeSources;
    std::condition_variable m_updateCv;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
};

class AudioSource {
public:
    bool load(const std::string& filepath);

    void play();
    void pause();
    void stop();

    void seek(double seconds);

    void setVolume(float volume);
    void setPan(float pan);
    void setPitch(float pitch);
    void setLoop(bool enabled);

    bool isPlaying() const;
    bool isPaused() const;
    bool isStopped() const;

    double position() const;
    double duration() const;
    float volume() const;
    const std::string& currentFile();
    bool loop() const;

    explicit AudioSource(AudioEngine* engine);
    ~AudioSource() noexcept;

private:
    friend class AudioEngine;

    bool openFile(const std::string& path);
    int decodeNextBlock(std::span<int16_t> outBuffer, int maxSamples);
    void updateOpenALParams();
    void refillBuffers();
    void clearBuffers();

    AudioEngine* m_engine{ nullptr };

    ALuint m_source{ 0 };
    std::vector<ALuint> m_buffers;

    AVFormatContext* m_fmt{ nullptr };
    AVCodecContext* m_codec{ nullptr };
    SwrContext* m_swr{ nullptr };
    int m_streamIdx{ -1 };
    int m_outputChannels{ 2 };
    int m_outputSampleRate{ 0 };
    ALenum m_outputFormat{ AL_FORMAT_STEREO16 };

    std::vector<int16_t> m_decodeBuffer;

    std::atomic<bool> m_playing{ false };
    std::atomic<bool> m_paused{ false };
    std::atomic<bool> m_loop{ false };
    std::atomic<float> m_volume{ 1.0f };
    std::atomic<float> m_pan{ 0.0f };
    std::atomic<float> m_pitch{ 1.0f };

    std::atomic<int64_t> m_playedSamples{ 0 };
    int64_t m_totalSamples{ 0 };

    std::mutex m_stateMutex;
    std::string m_currentFile;

    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;
};