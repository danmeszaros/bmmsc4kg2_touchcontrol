#include "camera.h"

#include <cmath>
#include <cstdarg>
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
constexpr uint32_t kNoteMs = 2500;         // on-screen diagnostic line
// While recording only the record state is polled (to notice a stop on the
// camera itself) plus the one read-back after each change from the touch UI;
// media space etc. is on the camera's own viewfinder.
constexpr uint32_t kRecPollMs = 1000;
constexpr uint32_t kReadbackMs = 100;     // after the edit hold (iris lens needs ~0.3 s, hold is longer)
constexpr uint32_t kProbeMs = 1000;       // while offline
constexpr int kOfflineAfter = 3;          // consecutive failures

struct OpDef { const char* method; const char* path; const char* body; };

// indexed by Camera::Op; %d = audio channel
constexpr OpDef kOps[] = {
    {"GET", "transports/0/record", nullptr},
    {"GET", "lens/iris", nullptr},
    {"GET", "lens/zoom", nullptr},
    {"GET", "video/whiteBalance", nullptr},
    {"GET", "video/whiteBalanceTint", nullptr},
    {"GET", "video/gain", nullptr},
    {"GET", "system/format", nullptr},
    {"GET", "media/workingset", nullptr},
    {"GET", "audio/channel/%d/input", nullptr},
    {"GET", "audio/channel/%d/level", nullptr},
    {"GET", "audio/channel/%d/input/description", nullptr},
    {"GET", "video/autoExposure", nullptr},
    {"PUT", "video/autoExposure", "{\"mode\":\"Off\",\"type\":\"\"}"},
    {"PUT", "video/shutter", "{\"shutterAngle\":18000}"},
    {"PUT", "transports/0/record", "{\"recording\":true}"},
    {"PUT", "transports/0/record", "{\"recording\":false}"},
    {"PUT", "lens/focus/doAutoFocus", nullptr},
    {"PUT", "lens/iris", nullptr},
    {"PUT", "video/whiteBalance", nullptr},
    {"PUT", "video/whiteBalance/doAuto", nullptr},
    {"PUT", "video/gain", nullptr},
    {"PUT", "audio/channel/%d/input", nullptr},
    {"PUT", "audio/channel/%d/level", nullptr},
    {"PUT", "system/format", nullptr},
    {"PUT", "system/format", nullptr}, // codec: same object, other field
};
static_assert(sizeof(kOps) / sizeof(kOps[0]) == 25, "kOps must match Camera::Op");

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

std::string pathOf(const OpDef& d, int ch) {
    char p[64];
    std::snprintf(p, sizeof p, d.path, ch);
    return p;
}

} // namespace

void Camera::setLink(bool up, uint32_t nowMs) {
    now_ = nowMs;
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

void Camera::enqueue(Op op, uint8_t ch, std::string body) {
    // a newer value for the same setting replaces the queued one
    for (int i = 0; i < qLen_; ++i) {
        Cmd& c = queue_[(qHead_ + i) % kQueue];
        if (c.op == op && c.ch == ch) {
            c.body = std::move(body);
            return;
        }
    }
    if (qLen_ == kQueue) {
        printf("cam: queue full, dropped %s\n", kOps[(int)op].path);
        return;
    }
    Cmd& c = queue_[(qHead_ + qLen_) % kQueue];
    c.op = op;
    c.ch = ch;
    c.body = std::move(body);
    ++qLen_;
}

bool Camera::queued(Op op) const {
    for (int i = 0; i < qLen_; ++i) {
        if (queue_[(qHead_ + i) % kQueue].op == op) return true;
    }
    return inFlight_ && cur_.op == op;
}

bool Camera::readbackOf(Op set, uint8_t ch, Op& get, Hold& h) {
    switch (set) {
    case Op::PutIris: get = Op::GetIris; h = H_IRIS; return true;
    case Op::PutWb: get = Op::GetWb; h = H_WB; return true;
    case Op::PutGain: get = Op::GetGain; h = H_GAIN; return true;
    case Op::PutFormat: get = Op::GetFormat; h = H_FPS; return true;
    case Op::PutCodec: get = Op::GetFormat; h = H_CODEC; return true;
    case Op::PutAudioInput: get = Op::GetAudioInput; h = (Hold)(H_AIN0 + ch); return ch < 2;
    case Op::PutAudioLevel: get = Op::GetAudioLevel; h = (Hold)(H_ALVL0 + ch); return ch < 2;
    default: return false;
    }
}

uint32_t Camera::pollPeriod(const Poll& p) const {
    return st_.recording && p.op == Op::GetRecord ? kRecPollMs : p.periodMs;
}

void Camera::pollSoon(Op op, uint8_t ch, uint32_t inMs) {
    for (int i = 0; i < pollCount_; ++i) {
        if (polls_[i].op == op && polls_[i].ch == ch) {
            polls_[i].due = now_ + inMs;
            polls_[i].soon = true;
        }
    }
}

void Camera::startRequest(const Cmd& cmd, bool freshConn, uint32_t nowMs) {
    const OpDef& d = kOps[(int)cmd.op];
    auto req = std::make_unique<HttpRequest>();
    std::vector<std::string> headers{"Accept: application/json"};
    std::string body = !cmd.body.empty() ? cmd.body : (d.body ? d.body : "");
    if (!body.empty()) headers.push_back("Content-Type: application/json");
    req->bytes = HttpRequest::build(d.method, std::string(kApi) + pathOf(d, cmd.ch), kHost, headers, body);
    req->id = (uint16_t)cmd.op;
    req->timeoutMs = kTimeoutMs;
    req->policy = ConnPolicy::Keep;
    req->newConnection = freshConn;
    if (http_.start(std::move(req))) {
        inFlight_ = true;
        cur_ = cmd;
        curFresh_ = freshConn;
        for (int i = 0; i < pollCount_; ++i) {
            if (polls_[i].op == cmd.op && polls_[i].ch == cmd.ch) {
                polls_[i].due = nowMs + pollPeriod(polls_[i]);
                polls_[i].soon = false;
            }
        }
    }
}

void Camera::onHttpDone(const HttpRequest& req) {
    // only copy out; acted upon in poll()
    haveDone_ = true;
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
    const Poll table[] = {
        {Op::GetRecord, 0, 300, nowMs},     {Op::GetIris, 0, 1000, nowMs},
        {Op::GetZoom, 0, 2000, nowMs},      {Op::GetWb, 0, 1000, nowMs},
        {Op::GetTint, 0, 2000, nowMs},      {Op::GetGain, 0, 1000, nowMs},
        {Op::GetFormat, 0, 5000, nowMs},    {Op::GetWorkingset, 0, 5000, nowMs},
        {Op::GetAudioInput, 0, 3000, nowMs}, {Op::GetAudioInput, 1, 3000, nowMs},
        {Op::GetAudioLevel, 0, 2000, nowMs}, {Op::GetAudioLevel, 1, 2000, nowMs},
        {Op::GetAudioDesc, 0, 30000, nowMs}, {Op::GetAudioDesc, 1, 30000, nowMs},
    };
    pollCount_ = 0;
    for (const Poll& p : table) polls_[pollCount_++] = p;
    changed();
}

void Camera::wentOffline() {
    if (st_.online) printf("cam: offline\n");
    const bool link = st_.link;
    const uint32_t seq = st_.seq;
    const CamState old = st_;
    st_ = CamState();
    std::memcpy(st_.note, old.note, sizeof st_.note);
    st_.noteUntilMs = old.noteUntilMs;
    st_.link = link;
    st_.seq = seq;
    qLen_ = 0;
    failures_ = 0;
    pollCount_ = 0;
    formatBody_.clear();
    changed();
}

const char* Camera::buttonOpName(Op op) {
    switch (op) {
    case Op::PutRecordOn: return "REC start";
    case Op::PutRecordOff: return "REC stop";
    case Op::PutAutoFocus: return "AF";
    default: return nullptr;
    }
}

void Camera::handleOk(const Cmd& cmd, int status, const std::string& body, uint32_t nowMs) {
    const Op op = cmd.op;
    if (const char* n = buttonOpName(op)) note(nowMs, "%s -> %d", n, status);
    Op get;
    Hold h;
    if (readbackOf(op, cmd.ch, get, h)) {
        // read the setting back once the local edit hold is over (polled values
        // are ignored before that); a rejected setter: right away
        if (status >= 300) holdUntil_[h] = 0;
        const int32_t left = (int32_t)(holdUntil_[h] - nowMs);
        pollSoon(get, cmd.ch, (left > 0 ? (uint32_t)left : 0) + kReadbackMs);
    }
    if (status >= 300) {
        printf("cam: %s %s -> %d %.60s\n", kOps[(int)op].method, pathOf(kOps[(int)op], cmd.ch).c_str(), status,
               body.c_str());
        if (op == Op::PutRecordOn) recStartFailed();
        return;
    }
    double d;
    bool b;
    std::string s;
    AudioChannel* a = cmd.ch < 2 ? &st_.audio[cmd.ch] : nullptr;
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
    case Op::GetIris: {
        double n = st_.irisNorm;
        json::number(body, "normalised", n);
        if (!held(H_IRIS) && json::number(body, "apertureStop", d) &&
            (!st_.irisKnown || std::fabs(st_.irisStop - d) > 0.01 || std::fabs(st_.irisNorm - n) > 0.005)) {
            st_.irisStop = (float)d;
            st_.irisNorm = (float)n;
            st_.irisKnown = true;
            changed();
        }
        break;
    }
    case Op::GetZoom:
        if (json::number(body, "focalLength", d) && (!st_.zoomKnown || st_.focalLength != (int)d)) {
            st_.focalLength = (int)d;
            st_.zoomKnown = true;
            changed();
        }
        break;
    case Op::GetWb:
        if (!held(H_WB) && json::number(body, "whiteBalance", d) && (!st_.wbKnown || st_.wb != (int)d)) {
            st_.wb = (int)d;
            st_.wbKnown = true;
            changed();
        }
        break;
    case Op::GetTint:
        if (!held(H_TINT) && json::number(body, "whiteBalanceTint", d) && (!st_.tintKnown || st_.tint != (int)d)) {
            st_.tint = (int)d;
            st_.tintKnown = true;
            changed();
        }
        break;
    case Op::GetGain:
        if (!held(H_GAIN) && json::number(body, "gain", d) && (!st_.gainKnown || st_.gain != (int)d)) {
            st_.gain = (int)d;
            st_.gainKnown = true;
            changed();
        }
        break;
    case Op::GetFormat:
        formatBody_ = body;
        if (!held(H_FPS) && json::string(body, "frameRate", s) && s != st_.fps) {
            std::snprintf(st_.fps, sizeof st_.fps, "%s", s.c_str());
            changed();
        }
        if (!held(H_CODEC) && json::string(body, "codec", s) && s != st_.codec) {
            std::snprintf(st_.codec, sizeof st_.codec, "%s", s.c_str());
            changed();
        }
        break;
    case Op::GetWorkingset:
        if (json::number(body, "remainingRecordTime", d) && st_.remainingSec != (int32_t)d) {
            st_.remainingSec = (int32_t)d;
            changed();
        }
        break;
    case Op::GetAudioInput:
        if (a && !held((Hold)(H_AIN0 + cmd.ch)) && json::string(body, "input", s) &&
            (!a->inputKnown || s != a->input)) {
            std::snprintf(a->input, sizeof a->input, "%s", s.c_str());
            a->inputKnown = true;
            a->rangeKnown = false;
            pollSoon(Op::GetAudioDesc, cmd.ch, 0);
            pollSoon(Op::GetAudioLevel, cmd.ch, 0);
            changed();
        }
        break;
    case Op::GetAudioLevel:
        if (a && !held((Hold)(H_ALVL0 + cmd.ch)) && json::number(body, "gain", d) &&
            (!a->gainKnown || std::fabs(a->gain - d) > 0.05)) {
            a->gain = (float)d;
            a->gainKnown = true;
            changed();
        }
        break;
    case Op::GetAudioDesc: {
        double mn, mx;
        if (a && json::number(body, "Min", mn) && json::number(body, "Max", mx)) {
            if (!a->rangeKnown || a->gainMin != (float)mn || a->gainMax != (float)mx) {
                a->gainMin = (float)mn;
                a->gainMax = (float)mx;
                a->rangeKnown = true;
                changed();
            }
        }
        break;
    }
    case Op::GetAutoExposure:
        if (json::string(body, "mode", s) && s != "Off") {
            printf("cam: auto exposure was %s, switching off\n", s.c_str());
            enqueue(Op::PutAutoExposureOff);
        }
        break;
    case Op::PutRecordOn:
        break; // no media read while recording
    case Op::PutRecordOff:
        pollSoon(Op::GetWorkingset, 0, 0); // clip count / space changed
        break;
    case Op::PutAutoWb:
        holdUntil_[H_WB] = holdUntil_[H_TINT] = 0;
        pollSoon(Op::GetWb, 0, 300); // result is there after ~0.2 s
        pollSoon(Op::GetTint, 0, 300);
        break;
    case Op::PutAudioInput:
        pollSoon(Op::GetAudioDesc, cmd.ch, 0); // gain range and gain follow the input
        holdUntil_[H_ALVL0 + cmd.ch] = 0;
        pollSoon(Op::GetAudioLevel, cmd.ch, 0);
        break;
    case Op::PutFormat:
        enqueue(Op::PutShutter180); // the camera keeps the shutter time, not the angle
        break;
    case Op::PutCodec:
        pollSoon(Op::GetWorkingset, 0, 500); // remaining time depends on the codec
        break;
    default:
        break;
    }
}

void Camera::poll(uint32_t nowMs) {
    now_ = nowMs;
    http_.poll();

    if (haveDone_) {
        haveDone_ = false;
        inFlight_ = false;
        const Cmd cmd = cur_;
        if (doneResult_ == HttpResult::Ok) {
            failures_ = 0;
            if (!st_.online) wentOnline(nowMs);
            handleOk(cmd, doneStatus_, doneBody_, nowMs);
        } else {
            printf("cam: %s %s failed: %s%s\n", kOps[(int)cmd.op].method, pathOf(kOps[(int)cmd.op], cmd.ch).c_str(),
                   resultName(doneResult_), doneReused_ ? " (reused conn)" : "");
            if (const char* n = buttonOpName(cmd.op)) note(nowMs, "%s: %s", n, resultName(doneResult_));
            if (doneResult_ != HttpResult::LinkDown && doneReused_ && !curFresh_) {
                // stale kept connection: retry once right away on a new one
                startRequest(cmd, true, nowMs);
                return;
            }
            if (cmd.op == Op::PutRecordOn) recStartFailed();
            if (++failures_ >= kOfflineAfter && st_.online) wentOffline();
            nextProbeMs_ = nowMs + kProbeMs;
        }
    }

    if (!st_.link || inFlight_ || http_.busy()) return;

    if (!st_.online) {
        // probe until the camera answers
        if ((int32_t)(nowMs - nextProbeMs_) >= 0) {
            nextProbeMs_ = nowMs + kProbeMs;
            Cmd probe;
            probe.op = Op::GetRecord;
            startRequest(probe, true, nowMs);
        }
        return;
    }

    if (qLen_ > 0) {
        Cmd cmd = std::move(queue_[qHead_]);
        qHead_ = (qHead_ + 1) % kQueue;
        --qLen_;
        startRequest(cmd, false, nowMs);
        return;
    }

    // most overdue poll
    int best = -1;
    int32_t bestLate = -1;
    for (int i = 0; i < pollCount_; ++i) {
        if (st_.recording && polls_[i].op != Op::GetRecord && !polls_[i].soon) continue;
        int32_t late = (int32_t)(nowMs - polls_[i].due);
        if (late >= 0 && late > bestLate) {
            best = i;
            bestLate = late;
        }
    }
    if (best >= 0) {
        Cmd c;
        c.op = polls_[best].op;
        c.ch = polls_[best].ch;
        startRequest(c, false, nowMs);
    }
}

void Camera::toggleRecord(uint32_t nowMs) {
    now_ = nowMs;
    if (!st_.online) {
        note(nowMs, "REC: camera offline");
        return;
    }
    if ((int32_t)(nowMs - recLockUntilMs_) < 0 || queued(Op::PutRecordOn) || queued(Op::PutRecordOff)) {
        printf("cam: rec button ignored (busy)\n");
        note(nowMs, "REC ignored (busy)");
        return;
    }
    const bool start = !st_.recording;
    enqueue(start ? Op::PutRecordOn : Op::PutRecordOff);
    note(nowMs, start ? "REC start sent" : "REC stop sent");
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

void Camera::recStartFailed() {
    // undo the optimistic START at once; the next poll shows the real state
    st_.recording = false;
    recLockUntilMs_ = 0;
    pollSoon(Op::GetRecord, 0, 0);
    changed();
}

void Camera::autoFocus(uint32_t nowMs) {
    now_ = nowMs;
    if (!st_.online) {
        note(nowMs, "AF: camera offline");
        return;
    }
    enqueue(Op::PutAutoFocus);
    note(nowMs, "AF sent");
    printf("cam: autofocus\n");
}

void Camera::note(uint32_t nowMs, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(st_.note, sizeof st_.note, fmt, ap);
    va_end(ap);
    st_.noteUntilMs = nowMs + kNoteMs;
    changed();
}

void Camera::setIris(float stop) {
    if (!st_.online) return;
    char b[48];
    std::snprintf(b, sizeof b, "{\"apertureStop\":%.2f}", (double)stop);
    enqueue(Op::PutIris, 0, b);
    st_.irisStop = stop;
    hold(H_IRIS);
    changed();
}

void Camera::setWb(int kelvin) {
    if (!st_.online) return;
    char b[40];
    std::snprintf(b, sizeof b, "{\"whiteBalance\":%d}", kelvin);
    enqueue(Op::PutWb, 0, b);
    st_.wb = kelvin;
    hold(H_WB);
    changed();
}

void Camera::autoWb() {
    if (!st_.online) return;
    enqueue(Op::PutAutoWb);
    printf("cam: auto WB\n");
}

void Camera::setGain(int db) {
    if (!st_.online) return;
    char b[32];
    std::snprintf(b, sizeof b, "{\"gain\":%d}", db);
    enqueue(Op::PutGain, 0, b);
    st_.gain = db;
    hold(H_GAIN);
    changed();
}

void Camera::setAudioInput(int ch, const char* input) {
    if (!st_.online || ch < 0 || ch > 1) return;
    std::string b = std::string("{\"input\":\"") + input + "\"}";
    enqueue(Op::PutAudioInput, (uint8_t)ch, b);
    AudioChannel& a = st_.audio[ch];
    std::snprintf(a.input, sizeof a.input, "%s", input);
    a.inputKnown = true;
    a.rangeKnown = false; // new input, new range
    hold((Hold)(H_AIN0 + ch));
    changed();
}

void Camera::setAudioGain(int ch, float db) {
    if (!st_.online || ch < 0 || ch > 1) return;
    char b[40];
    std::snprintf(b, sizeof b, "{\"gain\":%.1f}", (double)db);
    enqueue(Op::PutAudioLevel, (uint8_t)ch, b);
    st_.audio[ch].gain = db;
    hold((Hold)(H_ALVL0 + ch));
    changed();
}

void Camera::setFps(const char* fps) {
    if (!st_.online) return;
    if (st_.recording) {
        // the camera can't change the format mid-take
        note(now_, "FPS: stop REC first");
        return;
    }
    std::string body;
    if (!formatWith("frameRate", fps, body)) {
        printf("cam: no format known yet, fps not set\n");
        pollSoon(Op::GetFormat, 0, 0);
        return;
    }
    enqueue(Op::PutFormat, 0, body);
    formatBody_ = body; // a quick second change builds on this one
    std::snprintf(st_.fps, sizeof st_.fps, "%s", fps);
    hold(H_FPS, 2000);
    printf("cam: fps %s\n", fps);
    changed();
}

bool Camera::formatWith(const char* key, const char* value, std::string& out) const {
    // the camera only accepts the complete format object: take the last GET
    // and replace one string field
    out = formatBody_;
    const std::string k = std::string("\"") + key + "\"";
    size_t at = out.find(k);
    size_t colon = at == std::string::npos ? at : out.find(':', at + k.size());
    size_t q1 = colon == std::string::npos ? colon : out.find('"', colon);
    size_t q2 = q1 == std::string::npos ? q1 : out.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;
    out.replace(q1 + 1, q2 - q1 - 1, value);
    return true;
}

void Camera::setCodec(const char* codec) {
    if (!st_.online) return;
    if (st_.recording) {
        note(now_, "BRAW: stop REC first");
        return;
    }
    std::string body;
    if (!formatWith("codec", codec, body)) {
        printf("cam: no format known yet, codec not set\n");
        pollSoon(Op::GetFormat, 0, 0);
        return;
    }
    enqueue(Op::PutCodec, 0, body);
    formatBody_ = body;
    std::snprintf(st_.codec, sizeof st_.codec, "%s", codec);
    hold(H_CODEC, 2000);
    printf("cam: codec %s\n", codec);
    changed();
}
