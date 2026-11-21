// What the controller knows about the camera. Filled by Camera, drawn by Ui.
#pragma once

#include <cstdint>

struct CamState {
    bool link = false;       // camera has configured our USB device
    bool online = false;     // camera answers HTTP requests

    bool recKnown = false;
    bool recording = false;

    bool irisKnown = false;
    float irisStop = 0;      // f-number

    bool wbKnown = false;
    int wb = 0;              // kelvin
    bool tintKnown = false;
    int tint = 0;

    bool gainKnown = false;
    int gain = 0;            // dB

    char fps[8] = "";        // as reported, e.g. "25"
    int32_t remainingSec = -1; // remaining record time on the active disk

    uint32_t seq = 0;        // bumped on every change (redraw trigger)
};
