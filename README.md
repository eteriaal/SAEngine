# SAEngine — Simple Audio Engine

<p align="center">
  <img src="https://img.shields.io/badge/License-MIT-green" />
  <img src="https://img.shields.io/badge/C++-20-blue" />
  <img src="https://img.shields.io/badge/FFmpeg-required-orange" />
  <img src="https://img.shields.io/badge/OpenAL-required-orange" />
</p>

Single-file audio playback lib with high-level API, using OpenAL & FFmpeg.  
Designed as a reusable backend for small projects and experiments.  

> ⚠️ Early development, hobby project.  
> ⚠️ Many features are still WIP; (you can check TO-DO in SAEngine.h)  
> ⚠️ API is unstable.

---

## Features

- **Single-file(header)** - SAEngine.h
- **Almost all audio formats** via FFmpeg (MP3, WAV, OGG, FLAC, M4A, Opus....)
- **Streaming playback** - no need to load entire file into memory
- **Multiple audio sources** - create as many 'AudioSource' as you want
- **Controls** - play/pause/stop/seek/volume/pitch/pan/loop + masterVolume/pauseAll/resumeALL/stopALL in engine.
- **Thread-safe** source management

## How to use?
Just copy SAEngine.h into project  

In exactly one .cpp file:
```cpp
#define SAE_IMPLEMENTATION
#include "SAEngine.h"
```
If you want to include header in other files, do not use `#define SAE_IMPLEMENTATION`:
```cpp
#include "SAEngine.h"
```

## Requirements?

- **Compiler with C++20 support**
- **FFmpeg development libs (`libavformat`, `libavcodec`, `libavutil`, `libswresample`)**
- **OpenAL (OpenAL Soft)**


## Usage (Quick start)
```cpp
#define SAE_IMPLEMENTATION
#include "SAEngine.h"

int main() {
    AudioEngine engine(8192, 4, SampleRate::Hz48000); // Recommended stable values until async decoding is added
    auto source = engine.createSource();
    
    if (!source->load("music.flac")) {
        std::cerr << "Failed to load file\n";
        return 1;
    }
    source->play();

    source->setVolume(0.7f);

    // Simple blocking wait
    while (source->isPlaying()) {
        engine.update(); // required until built-in worker thread
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return 0;
}
```

## Compile example (Linux)
```bash
g++ -std=c++20 main.cpp -o SAEexample \ 
    -lopenal -lavformat -lavcodec -lavutil -lswresample -lstdc++ -lm -lpthread
```

## API Overview  

### AudioEngine (global manager)
```cpp
AudioEngine(size_t bufferSamples = 8192, int numBuffers = 4, SampleRate rate = SampleRate::Hz48000 )
std::unique_ptr<AudioSource> createSource()     // creates new independent source
void update()                                   // refill buffers (call regularly)
void setMasterVolume(float) / float masterVolume()
void pauseAll() / resumeAll() / stopAll()
```

### AudioSource (individual track/player)
```cpp
bool load(const std::string& filepath);          // load file (supports most formats via FFmpeg)

void play();                                     // start/resume
void pause();                                    // pause (keep position)
void stop();                                     // stop & reset position

void seek(double seconds);                       // jump to time

void setVolume(float);                           // 0.0f..1.0f
void setPan(float);                              // -1.0f (left) .. 1.0f (right)
void setPitch(float);                            // >1.0 = faster/higher, <1.0 = slower/lower
void setLoop(bool);

bool isPlaying() / isPaused() / isStopped() const;
double position() const;                         // current time in seconds
double duration() const;                         // total length or -1 if unknown
float volume() const;
bool loop() const;
const std::string& currentFile() const;
```

All controls are thread-safe (protected by mutexes).  
Full details -> see SAEngine.h

Also, library has SAE_VERSION & SAE_VERSION_STR, which can be output:
```cpp
std::cout << SAE_VERSION_STR << "\n";
```

## Configuration Macros
Define these before `#include "SAEngine.h"` to customize the library:

- `#define SAE_IMPLEMENTATION`  
Required in exactly one .cpp file to include the full implementation code.  
In other files — just #include "SAEngine.h" without it.

- `#define SAE_DEBUG_LOG`  
Enables debug logs (OpenAL info, underrun warnings, etc.).  
Will be improved in future.
- `#define NDEBUG`  
Disables OpenAL error checking (checkALError / checkALCError).  


Future macros (planned, see TO-DO): 
- `#define SAE_FORCE_MONO_DOWNMIX` — Force mono downmix for panning.
- `#define SAE_CACHING` — Enable optional PCM caching.
- `#define SAE_METADATA` — Enable metadata reading (title/artist/etc.).

## License
MIT License. See [LICENSE](LICENSE) or the top of SAEngine.h for details.