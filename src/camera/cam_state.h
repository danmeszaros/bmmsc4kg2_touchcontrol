// What the controller knows about the camera. Filled by Camera, drawn by Ui.
#pragma once

#include <cstdint>

struct AudioChannel {
    bool inputKnown = false;
    char input[28] = "";     // e.g. "3.5mm Left - Mic"
    bool gainKnown = false;
    float gain = 0;          // dB
    bool rangeKnown = false;
    float gainMin = 0, gainMax = 0; // for the current input
};

struct CamState {
    bool link = false;       // camera has configured our USB device
    bool online = false;     // camera answers HTTP requests

    bool recKnown = false;
    bool recording = false;

    bool irisKnown = false;
    float irisStop = 0;      // f-number
    float irisNorm = 0;      // 0 = widest the lens can do (at this zoom), 1 = narrowest

    bool zoomKnown = false;
    int focalLength = 0;     // mm

    bool wbKnown = false;
    int wb = 0;              // kelvin
    bool tintKnown = false;
    int tint = 0;

    bool gainKnown = false;
    int gain = 0;            // dB

    char fps[8] = "";        // as reported, e.g. "25"
    char codec[16] = "";     // as reported, e.g. "BRaw:8_1" or "BRaw:Q3"
    int32_t remainingSec = -1; // remaining record time on the active disk

    AudioChannel audio[2];   // camera channel 1 = [0]

    // short diagnostic line (button presses and their results), shown until noteUntilMs
    char note[32] = "";
    uint32_t noteUntilMs = 0;

    uint32_t seq = 0;        // bumped on every change (redraw trigger)
};

// What the UI may ask the camera to do. Implemented by Camera; host tests
// use a fake.
class CameraCtl {
public:
    virtual ~CameraCtl() = default;
    virtual void setIris(float stop) = 0;
    virtual void setWb(int kelvin) = 0;
    virtual void autoWb() = 0;
    virtual void setGain(int db) = 0;
    virtual void setAudioInput(int ch, const char* input) = 0;
    virtual void setAudioGain(int ch, float db) = 0;
    virtual void setFps(const char* fps) = 0;
    virtual void setCodec(const char* codec) = 0;
};
