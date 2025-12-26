#include "SAEngine.h"

int main() {
    setlocale(LC_ALL, "");
    
    AudioEngine engine(8192, 4, SampleRate::Hz48000);
    auto music = engine.createSource();

    std::string filePath = "your file path";
    music->load(filePath);

    engine.setMasterVolume(0.7f);
    music->play();

    while (music->isPlaying()) {
        engine.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return 0;
}