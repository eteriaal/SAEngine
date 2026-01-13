// main.cpp
// g++ -std=c++20 -O2 -Wall -Wextra main.cpp -o SAEexample -lopenal -lavformat -lavcodec -lavutil -lswresample -lstdc++ -lm -lpthread
#define SAE_IMPLEMENTATION
#define SAE_DEBUG_LOG
#include "SAEngine.h"

int main() {
    std::cout << SAE_VERSION_STR << "\n";
    AudioEngine engine(8192, 4, SampleRate::Hz48000);
    
    auto source = engine.createSource();
    if (!source->load("audio.flac")) {
        std::cerr << "Failed to load file\n";
        return 1;
    }

    engine.setMasterVolume(0.5f);
    source->setVolume(0.5f);
    source->setPan(0.0f);
    source->setPitch(1.1f);
    // source->setLoop(true);
    
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