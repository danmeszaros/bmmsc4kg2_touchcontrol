#include "camera.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "json.h"

namespace {

constexpr const char* kHost = "Micro-Studio-Camera-4K-G2.local";
constexpr const char* kApi = "/control/api/v1/";
constexpr uint32_t kTimeoutMs = 500;
constexpr uint32_t kRecLockMs = 1500;     // after STOP (camera needs ~1.2 s)
constexpr uint32_t kRecStartLockMs = 500; // after START
constexpr uint32_t kProbeMs = 1000;   // while offline
constexpr int kOfflineAfter = 3;      // consecutive failures

// poll period per polled Op, ms
constexpr uint32_t kPollMs[] = {
    300,  // GetRecord
    1000, // GetIris
    1000, // GetWb
    2000, // GetTint
    1000, // GetGain
    5000, // GetFormat
    5000, // GetWorkingset
};

struct OpDef { const char* method; const char* path; const char* body; };

// indexed by Camera::Op
constexpr OpDef kOps[] = {
    {"GET", "transports/0/record", nullptr},
    {"GET", "lens/iris", nullptr},
    {"GET", "video/whiteBalance", nullptr},
    {"GET", "video/whiteBalanceTint", nullptr},
    {"GET", "video/gain", nullptr},
    {"GET", "system/format", nullptr},
    {"GET", "media/workingset", nullptr},
    {"GET", "video/autoExposure", nullptr},
    {"PUT", "video/autoExposure", "{\"mode\":\"Off\",\"type\":\"\"}"},
    {"PUT", "video/shutter", "{\"shutterAngle\":18000}"},
    {"PUT", "transports/0/record", "{\"recording\":true}"},
    {"PUT", "transports/0/record", "{\"recording\":false}"},
    {"PUT", "lens/focus/doAutoFocus", nullptr},
};

const char* resultName(HttpResult r) {
    switch (r) {
    case HttpResult::Ok: return "ok";
    case HttpResult::ConnectFailed: return "connect_failed";
    case HttpResult::WriteFailed: return "write_failed";
    case HttpResult::Reset: return "reset";
    case HttpResult::ClosedEarly: return "closed_early";
    case HttpResult::Timeout: return "timeout";
    case HttpResult::TooLarge: return "too_large";
    case HttpResult::Malformed: return "malformed";
    case HttpResult::LinkDown: return "link_down";
    default: return "?";
    }
}

} // namespace

void Camera::setLink(bool up, uint32_t nowMs) {
    if (up == st_.link) return;
    st_.link = up;
    printf("cam: usb %s\n", up ? "up" : "down");
    if (!up) {
        http_.abortAll(HttpResult::LinkDown);
        wentOffline();
    } else {
        nextProbeMs_ = nowMs + 500; // give the camera time for DHCP
    }
    changed();
}

void Camera::onLease(const uint8_t ip[4]) {
    ip_addr_t a;
    IP_ADDR4(&a, ip[0], ip[1], ip[2], ip[3]);
    http_.setTarget(a, 80);
    printf("cam: lease %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
}

void Camera::enqueue(Op op) {
    if (queued(op) || qLen_ == kQueue) return;
    queue_[(qHead_ + qLen_) % kQueue] = op;
    ++qLen_;
}

bool Camera::queued(Op op) const {
    for (int i = 0; i < qLen_; ++i) {
        if (queue_[(qHead_ + i) % kQueue] == op) return true;
    }
    return inFlight_ && curOp_ == op;
}

void Camera::startRequest(Op op, bool freshConn, uint32_t nowMs) {
    const OpDef& d = kOps[(int)op];
    auto req = std::make_unique<HttpRequest>();
    std::vector<std::string> headers{"Accept: application/json"};
    std::string body = d.body ? d.body : "";
    if (!body.empty()) headers.push_back("Content-Type: application/json");
    req->bytes = HttpRequest::build(d.method, std::string(kApi) + d.path, kHost, headers, body);
    req->id = (uint16_t)op;
    req->timeoutMs = kTimeoutMs;
    req->policy = ConnPolicy::Keep;
    req->newConnection = freshConn;
    if (http_.start(std::move(req))) {
        inFlight_ = true;
        curOp_ = op;
        curFresh_ = freshConn;
        if ((int)op < kPollCount) pollDue_[(int)op] = nowMs + kPollMs[(int)op];
    }
}

void Camera::onHttpDone(const HttpRequest& req) {
    // only copy out; acted upon in poll()
    haveDone_ = true;
    doneOp_ = (Op)req.id;
    doneFresh_ = curFresh_;
    doneResult_ = req.result;
    doneReused_ = req.reused;
    doneStatus_ = req.parser.status();
    doneBody_.clear();
    size_t he = req.parser.headerEnd();
    if (req.result == HttpResult::Ok && he != std::string::npos && he <= req.response.size()) {
        doneBody_ = req.response.substr(he);
    }
}

void Camera::wentOnline(uint32_t nowMs) {
    st_.online = true;
    printf("cam: online\n");
    // manual exposure and 180 deg shutter every time the camera appears
    enqueue(Op::GetAutoExposure);
    enqueue(Op::PutShutter180);
    for (int i = 0; i < kPollCount; ++i) pollDue_[i] = nowMs;
    changed();
}

void Camera::wentOffline() {
    if (st_.online) printf("cam: offline\n");
    st_.online = false;
    st_.recKnown = st_.irisKnown = st_.wbKnown = st_.tintKnown = st_.gainKnown = false;
    st_.remainingSec = -1;
    st_.fps[0] = 0;
    qLen_ = 0;
    failures_ = 0;
    changed();
}

void Camera::handleOk(Op op, int status, const std::string& body, uint32_t nowMs) {
    if (status >= 300) {
        printf("cam: %s %s -> %d %.60s\n", kOps[(int)op].method, kOps[(int)op].path, status, body.c_str());
        return;
    }
    double d;
    bool b;
    std::string s;
    switch (op) {
    case Op::GetRecord:
        if (json::boolean(body, "recording", b)) {
            const bool locked = (int32_t)(nowMs - recLockUntilMs_) < 0;
            if (!locked && (!st_.recKnown || st_.recording != b)) {
                st_.recording = b;
                st_.recKnown = true;
                changed();
            }
        }
        break;
    case Op::GetIris:
        if (json::number(body, "apertureStop", d) && (!st_.irisKnown || std::fabs(st_.irisStop - d) > 0.01)) {
            st_.irisStop = (float)d;
            st_.irisKnown = true;
            changed();
        }
        break;
    case Op::GetWb:
        if (json::number(body, "whiteBalance", d) && (!st_.wbKnown || st_.wb != (int)d)) {
            st_.wb = (int)d;
            st_.wbKnown = true;
            changed();
        }
        break;
    case Op::GetTint:
        if (json::number(body, "whiteBalanceTint", d) && (!st_.tintKnown || st_.tint != (int)d)) {
            st_.tint = (int)d;
            st_.tintKnown = true;
            changed();
        }
        break;
    case Op::GetGain:
        if (json::number(body, "gain", d) && (!st_.gainKnown || st_.gain != (int)d)) {
            st_.gain = (int)d;
            st_.gainKnown = true;
            changed();
        }
        break;
    case Op::GetFormat:
        if (json::string(body, "frameRate", s) && s != st_.fps) {
            std::snprintf(st_.fps, sizeof st_.fps, "%s", s.c_str());
            changed();
        }
        break;
    case Op::GetWorkingset:
        if (json::number(body, "remainingRecordTime", d) && st_.remainingSec != (int32_t)d) {
            st_.remainingSec = (int32_t)d;
            changed();
        }
        break;
    case Op::GetAutoExposure:
        if (json::string(body, "mode", s) && s != "Off") {
            printf("cam: auto exposure was %s, switching off\n", s.c_str());
            enqueue(Op::PutAutoExposureOff);
        }
        break;
    case Op::PutRecordOn:
    case Op::PutRecordOff:
        pollDue_[(int)Op::GetWorkingset] = nowMs; // clip count / space changed
        break;
    default:
        break;
    }
}

void Camera::poll(uint32_t nowMs) {
    http_.poll();

    if (haveDone_) {
        haveDone_ = false;
        inFlight_ = false;
        if (doneResult_ == HttpResult::Ok) {
            failures_ = 0;
            if (!st_.online) wentOnline(nowMs);
            handleOk(doneOp_, doneStatus_, doneBody_, nowMs);
        } else {
            printf("cam: %s %s failed: %s%s\n", kOps[(int)doneOp_].method, kOps[(int)doneOp_].path,
                   resultName(doneResult_), doneReused_ ? " (reused conn)" : "");
            if (doneResult_ != HttpResult::LinkDown && doneReused_ && !doneFresh_) {
                // stale kept connection: retry once right away on a new one
                startRequest(doneOp_, true, nowMs);
                return;
            }
            if (++failures_ >= kOfflineAfter && st_.online) wentOffline();
            nextProbeMs_ = nowMs + kProbeMs;
        }
    }

    if (!st_.link || inFlight_ || http_.busy()) return;

    if (!st_.online) {
        // probe until the camera answers
        if ((int32_t)(nowMs - nextProbeMs_) >= 0) {
            nextProbeMs_ = nowMs + kProbeMs;
            startRequest(Op::GetRecord, true, nowMs);
        }
        return;
    }

    if (qLen_ > 0) {
        Op op = queue_[qHead_];
        qHead_ = (qHead_ + 1) % kQueue;
        --qLen_;
        startRequest(op, false, nowMs);
        return;
    }

    // most overdue poll
    int best = -1;
    int32_t bestLate = -1;
    for (int i = 0; i < kPollCount; ++i) {
        int32_t late = (int32_t)(nowMs - pollDue_[i]);
        if (late >= 0 && late > bestLate) {
            best = i;
            bestLate = late;
        }
    }
    if (best >= 0) startRequest((Op)best, false, nowMs);
}

void Camera::toggleRecord(uint32_t nowMs) {
    if (!st_.online) return;
    if ((int32_t)(nowMs - recLockUntilMs_) < 0 || queued(Op::PutRecordOn) || queued(Op::PutRecordOff)) {
        printf("cam: rec button ignored (busy)\n");
        return;
    }
    const bool start = !st_.recording;
    enqueue(start ? Op::PutRecordOn : Op::PutRecordOff);
    // optimistic: show the new state at once
    st_.recording = start;
    st_.recKnown = true;
    // STOP: the camera keeps reporting true while it finishes the file.
    // START: a poll already in flight may still answer false; also stops a
    // double press from cutting a clip right after it started.
    recLockUntilMs_ = nowMs + (start ? kRecStartLockMs : kRecLockMs);
    printf("cam: rec %s\n", start ? "START" : "STOP");
    changed();
}

void Camera::autoFocus() {
    if (!st_.online) return;
    enqueue(Op::PutAutoFocus);
    printf("cam: autofocus\n");
}
