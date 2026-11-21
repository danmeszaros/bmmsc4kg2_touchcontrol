// Talks to the camera's REST API over the USB network (see
// docs/controller-guide.md): one kept-alive connection, one request at a
// time, commands before polls, one retry on a fresh connection.
//
// Call everything from the main loop (lwIP context).
#pragma once

#include <cstdint>

#include "cam_state.h"
#include "http_client.h"

class Camera : public HttpListener {
public:
    Camera() : http_(*this) {}

    void setLink(bool up, uint32_t nowMs);
    void onLease(const uint8_t ip[4]);
    void poll(uint32_t nowMs);

    // buttons
    void toggleRecord(uint32_t nowMs);
    void autoFocus();

    const CamState& state() const { return st_; }

    // HttpListener
    void onHttpEvent(HttpEvent, uint16_t, uint16_t, int32_t, const uint8_t*, size_t) override {}
    void onHttpDone(const HttpRequest& req) override;

private:
    enum class Op : uint8_t {
        // polls
        GetRecord, GetIris, GetWb, GetTint, GetGain, GetFormat, GetWorkingset,
        // session setup
        GetAutoExposure, PutAutoExposureOff, PutShutter180,
        // commands
        PutRecordOn, PutRecordOff, PutAutoFocus,
        Count
    };
    static constexpr int kPollCount = (int)Op::GetWorkingset + 1;
    static constexpr int kQueue = 8;

    void enqueue(Op op);
    bool queued(Op op) const;
    void startRequest(Op op, bool freshConn, uint32_t nowMs);
    void handleOk(Op op, int status, const std::string& body, uint32_t nowMs);
    void wentOnline(uint32_t nowMs);
    void wentOffline();
    void changed() { ++st_.seq; }

    HttpClient http_;
    CamState st_;

    Op queue_[kQueue];
    int qHead_ = 0, qLen_ = 0;
    uint32_t pollDue_[kPollCount] = {};

    bool inFlight_ = false;
    Op curOp_ = Op::GetRecord;
    bool curFresh_ = false;

    // finished request, handled in poll() (not inside the HttpClient callback)
    bool haveDone_ = false;
    Op doneOp_ = Op::GetRecord;
    bool doneFresh_ = false;
    HttpResult doneResult_ = HttpResult::Pending;
    bool doneReused_ = false;
    int doneStatus_ = 0;
    std::string doneBody_;

    int failures_ = 0;
    uint32_t nextProbeMs_ = 0;
    // after STOP the camera reports recording=true for ~1.2 s while it
    // finishes the file: ignore the button and polled state until then
    uint32_t recLockUntilMs_ = 0;
};
