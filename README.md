# SAE — Simple Audio Engine
<p align="center">
  <img src="https://img.shields.io/badge/License-MIT-green" />
  <img src="https://img.shields.io/badge/C++-20-blue" />
  <img src="https://img.shields.io/badge/Windows-supported-blue" />
  <img src="https://img.shields.io/badge/Linux-supported-blue" />
  <img src="https://img.shields.io/badge/FFmpeg-required-orange" />
  <img src="https://img.shields.io/badge/OpenAL-required-orange" />
</p>



Simple, minimal audio engine built on top of **OpenAL** and **FFmpeg**.  
Designed as a reusable backend for small projects and experiments.  

Written in **C++20**.

> ⚠️ Experimental / hobby project.  
> No specialization.  
> Intended for learning and personal use.

---

## Features

- **Cross-platform playback**: Works on Windows and Linux via OpenAL.
- **Audio decoding with FFmpeg**: Supports MP3, WAV, OGG, M4A, and more.
- **Streaming playback**: Plays audio without loading the entire file into memory.
- **Multiple audio sources**: Create multiple `AudioSource` objects simultaneously.
- **Playback control**: Play, pause, stop, seek, loop.
- **Volume control**: Per-source volume and global master volume.
- **Thread-safe management**: Safe handling of multiple sources across threads.
- **Buffer management**: Automatic OpenAL buffer refill and decoding.
- **Debug info**: Optional verbose output about decoding and playback (#DEFINE SAE_DEBUG).
---

## Requirements

### Build-time
- CMake ≥ 3.28
- C++20 compiler

### Dependencies
- **FFmpeg**
  - libavcodec
  - libavformat
  - libavutil
  - libswresample
- **OpenAL**


## Building

```bash
git clone https://github.com/eteriaal/SAEngine.git
cd SAEngine
cmake -B build
cmake --build build
```
### Example
By default, a small example executable is OFF:
```bash
cmake -B build -DSAENGINE_BUILD_EXAMPLE=ON
cmake --build build
```
**! You also need to specify the audiofile path in main.cpp**

## Usage

SAE is built as a static library and can be embedded into other projects.
Example CMake integration:
```cmake
add_subdirectory(SAEngine)
target_link_libraries(your_target PRIVATE SAEngine)
```

## License
MIT License. See [LICENSE](LICENSE) for details.