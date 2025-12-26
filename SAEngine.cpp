// SAEngine.cpp from SimpleAudioEngine. https://github.com/eteriaal/SAEngine
#include "SAEngine.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>

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

// AudioEngine

AudioEngine::AudioEngine(size_t bufferSamples, int numBuffers, SampleRate engineSampleRate)
    : m_bufferSamples(bufferSamples),
      m_numBuffers(numBuffers),
      m_engineSampleRate(static_cast<int>(engineSampleRate)) {

    if (m_engineSampleRate != 0 && std::find(supportedRates.begin(), supportedRates.end(), engineSampleRate) == supportedRates.end()) {
        throw std::runtime_error("Unsupported sample rate");
    }

    m_device = alcOpenDevice(nullptr);
    if (!m_device) throw std::runtime_error("Failed to open ALC device");
    checkALCError(m_device, "alcOpenDevice");

    ALCint attrs[] = { ALC_FREQUENCY, m_engineSampleRate, 0 };
    m_context = alcCreateContext(m_device, m_engineSampleRate != 0 ? attrs : nullptr);
    if (!m_context) {
        alcCloseDevice(m_device);
        throw std::runtime_error("Failed to create ALC context");
    }
    checkALCError(m_device, "alcCreateContext");

    if (!alcMakeContextCurrent(m_context)) {
        alcDestroyContext(m_context);
        alcCloseDevice(m_device);
        throw std::runtime_error("Failed to make ALC context current");
    }
    checkALCError(m_device, "alcMakeContextCurrent");

#ifdef SAE_DEBUG
    std::cerr << "[AudioEngine] Initialized with sample rate: "
              << (std::to_string(m_engineSampleRate) + " Hz") << "\n";
#endif
}

AudioEngine::~AudioEngine() noexcept {
    {
        std::lock_guard<std::mutex> lock(m_sourcesMutex);
        for (auto source : m_activeSources) {
            source->stop();
            source->clearBuffers();
        }
        m_activeSources.clear();
    }

    alcMakeContextCurrent(nullptr);
    alcDestroyContext(m_context);
    alcCloseDevice(m_device);
}

void AudioEngine::update() {
	float masterVol = m_masterVolume.load(std::memory_order_relaxed);
	alListenerf(AL_GAIN, masterVol);
	checkALError("alListenerf AL_GAIN");

	std::vector<AudioSource*> sourcesCopy;
	{
		std::lock_guard<std::mutex> lock(m_sourcesMutex);
		sourcesCopy = m_activeSources;
	}

	for (AudioSource* source : sourcesCopy) {
		std::lock_guard<std::mutex> stateLock(source->m_stateMutex);

		if (!source->m_playing && !source->m_paused) {
			continue;
		}

		source->updateOpenALParams();
		checkALError("updateOpenALParams");

		source->refillBuffers();
		checkALError("refillBuffers");

		ALint state;
		alGetSourcei(source->m_source, AL_SOURCE_STATE, &state);
		checkALError("alGetSourcei AL_SOURCE_STATE");

		if (state == AL_STOPPED && source->m_playing && !source->m_loop) {
			source->stop();
			continue;
		}

		if (source->m_playing && !source->m_paused) {
			if (state == AL_INITIAL || state == AL_STOPPED) {
				alSourcePlay(source->m_source);
				checkALError("alSourcePlay");
			}
		}
	}
}

void AudioEngine::setMasterVolume(float volume) {
	m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
	m_updateCv.notify_one();
}

float AudioEngine::masterVolume() const {
	return m_masterVolume.load();
}

void AudioEngine::pauseAll() {
	m_globalPaused = true;
	std::lock_guard<std::mutex> lock(m_sourcesMutex);
	for (auto source : m_activeSources) {
		if (source->isPlaying()) {
			source->pause();
		}
	}
}
void AudioEngine::resumeAll() {
	m_globalPaused = false;
	std::lock_guard<std::mutex> lock(m_sourcesMutex);
	for (auto source : m_activeSources) {
		if (source->isPaused()) {
			source->play();
		}
	}
}
void AudioEngine::stopAll() {
	std::lock_guard<std::mutex> lock(m_sourcesMutex);
	for (auto source : m_activeSources) {
		source->stop();
	}
}

std::unique_ptr<AudioSource> AudioEngine::createSource() {
	auto source = std::make_unique<AudioSource>(this);
	registerSource(source.get());
	return source;
}

void AudioEngine::registerSource(AudioSource* source) {
	std::lock_guard<std::mutex> lock(m_sourcesMutex);
	m_activeSources.push_back(source);
	m_updateCv.notify_one();
}

void AudioEngine::unregisterSource(AudioSource* source) {
	std::lock_guard<std::mutex> lock(m_sourcesMutex);
	auto it = std::find(m_activeSources.begin(), m_activeSources.end(), source);
	if (it != m_activeSources.end()) {
		m_activeSources.erase(it);
	}
	m_updateCv.notify_one();
}

// AudioSource

AudioSource::AudioSource(AudioEngine* engine) : m_engine(engine) {
    alGenSources(1, &m_source);
    checkALError("alGenSources");

    m_buffers.resize(m_engine->m_numBuffers);
    alGenBuffers(static_cast<ALsizei>(m_buffers.size()), m_buffers.data());
    checkALError("alGenBuffers");

    m_decodeBuffer.resize(m_engine->m_bufferSamples * 2);

    m_engine->registerSource(this);
}

AudioSource::~AudioSource() noexcept {
    stop();
    clearBuffers();

    alDeleteSources(1, &m_source);
    alDeleteBuffers(static_cast<ALsizei>(m_buffers.size()), m_buffers.data());

    if (m_swr) swr_free(&m_swr);
    if (m_codec) avcodec_free_context(&m_codec);
    if (m_fmt) avformat_close_input(&m_fmt);

    m_engine->unregisterSource(this);
}

bool AudioSource::load(const std::string& filepath) {
    if (!openFile(filepath)) {
        std::cerr << "Failed to open audio file: " << filepath << std::endl;
        return false;
    }

    for (size_t i = 0; i < m_buffers.size(); ++i) {
        int decoded = decodeNextBlock(std::span<int16_t>(m_decodeBuffer), static_cast<int>(m_engine->m_bufferSamples));
        if (decoded > 0) {
            alBufferData(m_buffers[i], m_outputFormat, m_decodeBuffer.data(),
                         decoded * m_outputChannels * sizeof(int16_t), m_outputSampleRate);
            checkALError("alBufferData initial");
        } else {
            break;
        }
    }

    alSourceQueueBuffers(m_source, static_cast<ALsizei>(m_buffers.size()), m_buffers.data());
    checkALError("alSourceQueueBuffers initial");

    m_currentFile = filepath;
    return true;
}

void AudioSource::play() {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (m_engine->m_globalPaused) {
		return;
	}
	if (!m_playing && m_fmt) {
		m_playing = true;
		m_paused = false;
		refillBuffers();
		alSourcePlay(m_source);
		checkALError("alSourcePlay");
		m_engine->m_updateCv.notify_one();
	}
	else if (m_paused) {
		m_paused = false;
		alSourcePlay(m_source);
		checkALError("alSourcePlay");
	}
}

void AudioSource::pause() {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (m_playing) {
		alSourcePause(m_source);
		checkALError("alSourcePause");
		m_paused = true;
		m_playing = false;
	}
}

void AudioSource::stop() {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (m_playing || m_paused) {
		alSourceStop(m_source);
		checkALError("alSourceStop");
		clearBuffers();
		m_playing = false;
		m_paused = false;
		m_playedSamples = 0;
	}
}

void AudioSource::seek(double seconds) {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (!m_fmt || !m_codec) return;

	int64_t targetTs = static_cast<int64_t>(seconds * AV_TIME_BASE);
	if (av_seek_frame(m_fmt, m_streamIdx, targetTs, AVSEEK_FLAG_BACKWARD) < 0) {
		std::cerr << "Seek failed" << std::endl;
		return;
	}

	avcodec_flush_buffers(m_codec);
	m_playedSamples = static_cast<int64_t>(seconds * m_codec->sample_rate);
	if (m_playing) {
		clearBuffers();
		refillBuffers();
	}
}

void AudioSource::setVolume(float volume) {
	m_volume = std::clamp(volume, 0.0f, 1.0f);
	m_engine->m_updateCv.notify_one();
}

void AudioSource::setPan(float pan) {
	m_pan = std::clamp(pan, -1.0f, 1.0f);
	m_engine->m_updateCv.notify_one();
}

void AudioSource::setPitch(float pitch) {
	m_pitch = std::max(pitch, 0.1f);
	alSourcef(m_source, AL_PITCH, m_pitch);
	checkALError("alSourcef AL_PITCH");
}

void AudioSource::setLoop(bool enabled) {
	m_loop = enabled;
	alSourcei(m_source, AL_LOOPING, enabled ? AL_TRUE : AL_FALSE);
	checkALError("alSourcei AL_LOOPING");
}

bool AudioSource::isPlaying() const {
	return m_playing.load();
}

bool AudioSource::isPaused() const {
	return m_paused.load();
}

bool AudioSource::isStopped() const {
	return !m_playing && !m_paused;
}

double AudioSource::position() const {
	return static_cast<double>(m_playedSamples) / m_codec->sample_rate;
}

double AudioSource::duration() const {
	return static_cast<double>(m_totalSamples) / m_codec->sample_rate;
}

bool AudioSource::openFile(const std::string& path) {
    if (m_fmt) {
        avformat_close_input(&m_fmt);
        m_fmt = nullptr;
    }

    if (avformat_open_input(&m_fmt, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(m_fmt, nullptr) < 0) return false;

    m_streamIdx = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_streamIdx < 0) return false;

    AVStream* audio_stream = m_fmt->streams[m_streamIdx];
    const AVCodec* codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (!codec) return false;

    m_codec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codec, audio_stream->codecpar);
    if (avcodec_open2(m_codec, codec, nullptr) < 0) return false;

    m_outputChannels = m_codec->ch_layout.nb_channels;
    m_outputFormat = (m_outputChannels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

    int targetRate = m_engine->m_engineSampleRate;
    if (targetRate == 0) {
        targetRate = m_codec->sample_rate;
    }
    m_outputSampleRate = targetRate;

    if (m_swr) swr_free(&m_swr);
    m_swr = swr_alloc();

    AVChannelLayout out_layout{};
    av_channel_layout_default(&out_layout, m_outputChannels);

    int ret = swr_alloc_set_opts2(
        &m_swr,
        &out_layout,
        AV_SAMPLE_FMT_S16,
        m_outputSampleRate,
        &m_codec->ch_layout,
        m_codec->sample_fmt,
        m_codec->sample_rate,
        0, nullptr
    );

    if (ret < 0 || swr_init(m_swr) < 0) {
        std::cerr << "Failed to initialize SwrContext for file: " << path << std::endl;
        return false;
    }

    if (audio_stream->duration != AV_NOPTS_VALUE) {
        double seconds = audio_stream->duration * av_q2d(audio_stream->time_base);
        m_totalSamples = static_cast<int64_t>(seconds * m_outputSampleRate);
    } else {
        m_totalSamples = -1;
    }

    m_playedSamples = 0;

#ifdef SAE_DEBUG
    std::cerr << "[SAEngine] openFile SUCCESS for " << path 
              << " (rate: " << m_outputSampleRate << " Hz)\n";
#endif

    return true;
}

int AudioSource::decodeNextBlock(std::span<int16_t> outBuffer, int maxSamples) {
	if (!m_fmt || !m_codec || !m_swr) return 0;

	int totalSamples = 0;

	AVPacket* packet = av_packet_alloc();
	if (!packet) return 0;

	auto frame_deleter = [](AVFrame* f) { av_frame_free(&f); };
	std::unique_ptr<AVFrame, decltype(frame_deleter)> frame(av_frame_alloc(), frame_deleter);
	if (!frame) {
		av_packet_free(&packet);
		return 0;
	}

	while (totalSamples < maxSamples) {
		int ret = av_read_frame(m_fmt, packet);
		if (ret < 0) {
			if (ret == AVERROR_EOF) {
				if (m_loop) {
					seek(0);
					continue;
				}
			}
			else {
				char errbuf[128];
				av_strerror(ret, errbuf, sizeof(errbuf));
				std::cerr << "av_read_frame error: " << errbuf << std::endl;
			}
			break;
		}

		if (packet->stream_index != m_streamIdx) {
			av_packet_unref(packet);
			continue;
		}

		ret = avcodec_send_packet(m_codec, packet);
		av_packet_unref(packet);

		if (ret < 0) {
			char errbuf[128];
			av_strerror(ret, errbuf, sizeof(errbuf));
			std::cerr << "avcodec_send_packet error: " << errbuf << std::endl;
			break;
		}

		while (totalSamples < maxSamples) {
			ret = avcodec_receive_frame(m_codec, frame.get());
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			}
			if (ret < 0) {
				char errbuf[128];
				av_strerror(ret, errbuf, sizeof(errbuf));
				std::cerr << "avcodec_receive_frame error: " << errbuf << std::endl;
				break;
			}

			int needed = maxSamples - totalSamples;
			int outSamples = swr_get_out_samples(m_swr, frame->nb_samples);
			if (outSamples > needed) outSamples = needed;

			uint8_t* outData[8] = { nullptr };
			outData[0] = reinterpret_cast<uint8_t*>(outBuffer.data() + totalSamples * m_outputChannels);

			const uint8_t** inData = const_cast<const uint8_t**>(frame->data);

			int converted = swr_convert(m_swr, outData, outSamples, inData, frame->nb_samples);

			if (converted < 0) {
				char errbuf[128];
				av_strerror(converted, errbuf, sizeof(errbuf));
				std::cerr << "swr_convert error: " << errbuf << std::endl;
				break;
			}

			totalSamples += converted;
			m_playedSamples += converted;

			if (converted == 0) {
				break;
			}
		}

		if (ret == AVERROR_EOF) {
			break;
		}
	}

	av_packet_free(&packet);

#ifdef SAE_DEBUG
    if (totalSamples > 0) {
        double currentSeconds = static_cast<double>(m_playedSamples.load()) / m_outputSampleRate;
        double durationSeconds = (m_totalSamples > 0)
            ? static_cast<double>(m_totalSamples) / m_outputSampleRate
            : 0.0;

        std::string filename = m_currentFile;
        size_t slashPos = filename.find_last_of("/\\");
        if (slashPos != std::string::npos) {
            filename = filename.substr(slashPos + 1);
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);

        oss << "[SAEngine] Playing: " << filename << " | "
            << currentSeconds << "s";

        if (durationSeconds > 0.0) {
            int progress = static_cast<int>((currentSeconds / durationSeconds) * 100.0);

            oss << " / " << durationSeconds << "s [";

            const int barWidth = 30;
            int filled = (progress * barWidth) / 100;
            for (int i = 0; i < barWidth; ++i) {
                oss << (i < filled ? "#" : "-");
            }

            oss << "] " << progress << "%";
        } else {
            oss << " (duration unknown)";
        }

        oss << " | decoded: " << totalSamples << " samples";

        std::cerr << "\r" << std::setw(140) << std::left << oss.str() << std::flush;
    } else {
        std::cerr << "\n[SAEngine] Playback finished or paused (last decode returned 0 samples)\n";
    }
#endif

	return totalSamples;
}

void AudioSource::updateOpenALParams() {
	float effectiveVolume = m_volume * m_engine->m_masterVolume;
	alSourcef(m_source, AL_GAIN, effectiveVolume);
	checkALError("alSourcef AL_GAIN");

	float pan = m_pan;
	alSource3f(m_source, AL_POSITION, pan, 0.0f, 0.0f);
	checkALError("alSource3f AL_POSITION");
}


void AudioSource::refillBuffers() {
	ALint processed;
	alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &processed);
	checkALError("alGetSourcei AL_BUFFERS_PROCESSED");

	while (processed > 0) {
		ALuint buffer;
		alSourceUnqueueBuffers(m_source, 1, &buffer);
		checkALError("alSourceUnqueueBuffers");

		int samples = decodeNextBlock(std::span<int16_t>(m_decodeBuffer), static_cast<int>(m_engine->m_bufferSamples));

		if (samples > 0) {
			alBufferData(buffer, m_outputFormat, m_decodeBuffer.data(),
				samples * m_outputChannels * sizeof(int16_t), m_outputSampleRate);
			checkALError("alBufferData");
			alSourceQueueBuffers(m_source, 1, &buffer);
			checkALError("alSourceQueueBuffers");
		}
		else {
			alSourceQueueBuffers(m_source, 1, &buffer);
			checkALError("alSourceQueueBuffers (re-queue)");
		}

		--processed;
	}

	ALint queued;
	alGetSourcei(m_source, AL_BUFFERS_QUEUED, &queued);
	checkALError("alGetSourcei AL_BUFFERS_QUEUED");

	if (queued == 0 && m_playing) {
		for (size_t i = 0; i < m_buffers.size(); ++i) {
			int samples = decodeNextBlock(std::span<int16_t>(m_decodeBuffer), static_cast<int>(m_engine->m_bufferSamples));
			if (samples > 0) {
				alBufferData(m_buffers[i], m_outputFormat, m_decodeBuffer.data(),
					samples * m_outputChannels * sizeof(int16_t), m_outputSampleRate);
				checkALError("alBufferData");
				alSourceQueueBuffers(m_source, 1, &m_buffers[i]);
				checkALError("alSourceQueueBuffers");
			}
			else {
				break;
			}
		}
	}
}

void AudioSource::clearBuffers() {
	ALint queued;
	alGetSourcei(m_source, AL_BUFFERS_QUEUED, &queued);
	while (queued > 0) {
		ALuint buffer;
		alSourceUnqueueBuffers(m_source, 1, &buffer);
		--queued;
	}
	checkALError("clearBuffers");
}

float AudioSource::volume() const {
	return m_volume.load();
}

const std::string& AudioSource::currentFile() {
	return m_currentFile;
}

bool AudioSource::loop() const {
	return m_loop.load();
}