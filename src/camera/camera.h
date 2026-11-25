// Talks to the camera's REST API over the USB network (see
// docs/controller-guide.md): one kept-alive connection, one request at a
// time, commands before polls, one retry on a fresh connection.
//
// Setters update CamState at once (optimistic) and hold that field against
// polls for a moment, because the lens takes ~0.3 s and a poll already in
// flight would still report the old value.
//
// Call everything from the main loop (lwIP context).
#pragma once

#include <cstdint>
#include <string>

#include "cam_state.h"
#include "http_client.h"

class Camera : public HttpListener, public CameraCtl {
public:
    Camera() : http_(*this) {}

    void setLink(bool up, uint32_t nowMs);
    void onLease(const uint8_t ip[4]);
    void poll(uint32_t nowMs);

    // buttons
    void toggleRecord(uint32_t nowMs);
    void autoFocus(uint32_t nowMs);
    // on-screen diagnostic line for a moment
    void note(uint32_t nowMs, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

    // CameraCtl (touch UI)
    void setIris(float stop) override;
    void setWb(int kelvin) override;
    void autoWb() override;
    void setGain(int db) override;
    void setAudioInput(int ch, const char* input) override;
    void setAudioGain(int ch, float db) override;
    void setFps(const char* fps) override;
    void setCodec(const char* codec) override;

    const CamState& state() const { return st_; }

    // HttpListener
    void onHttpEvent(HttpEvent, uint16_t, uint16_t, int32_t, const uint8_t*, size_t) override {}
    void onHttpDone(const HttpRequest& req) override;

private:
    enum class Op : uint8_t {
        // polls
        GetRecord, GetIris, GetZoom, GetWb, GetTint, GetGain, GetFormat, GetWorkingset,
        GetAudioInput, GetAudioLevel, GetAudioDesc,
        // session setup
        GetAutoExposure, PutAutoExposureOff, PutShutter180,
        // commands
        PutRecordOn, PutRecordOff, PutAutoFocus,
        PutIris, PutWb, PutAutoWb, PutGain, PutAudioInput, PutAudioLevel, PutFormat, PutCodec,
        Count
    };
    struct Cmd {
        Op op = Op::GetRecord;
        uint8_t ch = 0;
        std::string body; // for setters; empty = fixed body from the table
    };
    struct Poll {
        Op op;
        uint8_t ch;
        uint32_t periodMs;
        uint32_t due;
        bool soon = false; // asked for by pollSoon (read-back): also runs while recording
    };
    // fields protected from polls after a local change
    enum Hold : uint8_t { H_IRIS, H_WB, H_TINT, H_GAIN, H_FPS, H_AIN0, H_AIN1, H_ALVL0, H_ALVL1, H_CODEC, H_COUNT };

    static constexpr int kQueue = 8;

    uint32_t pollPeriod(const Poll& p) const;
    // setter -> the poll that reads it back and the hold that guards it
    static bool readbackOf(Op set, uint8_t ch, Op& get, Hold& h);
    void recStartFailed(); // PUT record=true failed or was rejected
    static const char* buttonOpName(Op op); // REC/AF requests get an on-screen result
    void enqueue(Op op, uint8_t ch = 0, std::string body = std::string());
    bool queued(Op op) const;
    void startRequest(const Cmd& cmd, bool freshConn, uint32_t nowMs);
    void handleOk(const Cmd& cmd, int status, const std::string& body, uint32_t nowMs);
    void wentOnline(uint32_t nowMs);
    void wentOffline();
    void changed() { ++st_.seq; }
    void hold(Hold h, uint32_t ms = 1200) { holdUntil_[h] = now_ + ms; }
    bool held(Hold h) const { return (int32_t)(now_ - holdUntil_[h]) < 0; }
    void pollSoon(Op op, uint8_t ch, uint32_t inMs);
    // last GET system/format with one string field replaced; false if unknown
    bool formatWith(const char* key, const char* value, std::string& out) const;

    HttpClient http_;
    CamState st_;
    uint32_t now_ = 0;

    Cmd queue_[kQueue];
    int qHead_ = 0, qLen_ = 0;
    Poll polls_[14];
    int pollCount_ = 0;
    uint32_t holdUntil_[H_COUNT] = {};

    bool inFlight_ = false;
    Cmd cur_;
    bool curFresh_ = false;

    // finished request, handled in poll() (not inside the HttpClient callback)
    bool haveDone_ = false;
    HttpResult doneResult_ = HttpResult::Pending;
    bool doneReused_ = false;
    int doneStatus_ = 0;
    std::string doneBody_;

    std::string formatBody_; // last GET system/format, PUT back with a new frameRate / codec

    int failures_ = 0;
    uint32_t nextProbeMs_ = 0;
    // after STOP the camera reports recording=true for ~1.2 s while it
    // finishes the file: ignore the button and polled state until then
    uint32_t recLockUntilMs_ = 0;
};
