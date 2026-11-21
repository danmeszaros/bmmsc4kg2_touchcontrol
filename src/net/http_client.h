// HTTP/1.1 client on the lwIP raw TCP API (NO_SYS), one request in flight.
//
// Lifecycle guarantees:
//   - every started request ends exactly once (onHttpDone) with an HttpResult
//   - the pcb is detached (arg + callbacks cleared) before every close/abort,
//     so lwIP never calls back into a finished request or a dead client
//   - a pcb freed by lwIP (err callback) is forgotten immediately
//   - callbacks that abort their pcb return ERR_ABRT
//   - request bytes are written with TCP_WRITE_FLAG_COPY
//   - a timeout covers connect + send + receive
//
// Connection handling per request (ConnPolicy): close (FIN), abort (RST) or
// keep. A kept connection is reused by the next request unless it asks for a
// new one; a FIN/RST/data on the idle kept connection is reported as an event.
//
// All methods must run in the lwIP context (main loop on core0).

#pragma once

extern "C" {
#include <lwip/ip_addr.h>
#include <lwip/tcp.h>
}

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "http_parser.h"

// numeric values match proto::Result
enum class HttpResult : uint8_t {
    Pending = 0,
    Ok = 1,
    ConnectFailed = 2,
    WriteFailed = 3,
    Reset = 4,
    ClosedEarly = 5,
    Timeout = 6,
    TooLarge = 7,
    Malformed = 8,
    LinkDown = 9,
};

// numeric values match proto::Policy
enum class ConnPolicy : uint8_t { Close = 0, Abort = 1, Keep = 2 };

// numeric values match proto::Event
enum class HttpEvent : uint8_t {
    Connecting = 10,
    Connected = 11,
    Reused = 12,
    Acked = 13,
    Data = 14,
    PeerFin = 15,
    Err = 16,
    Close = 17,
    Abort = 18,
    Timeout = 19,
    IdleData = 20,
    ResponseComplete = 21,
    Note = 22,           // human readable decision/diagnostic, data = text
};

struct HttpRequest {
    // input
    uint16_t id = 0;               // caller's id, reported back in events
    std::string bytes;             // exact bytes to send
    uint32_t timeoutMs = 3000;
    ConnPolicy policy = ConnPolicy::Close;
    bool newConnection = false;    // never reuse a kept connection
    bool head = false;             // HEAD request: response has no body

    // output
    HttpResult result = HttpResult::Pending;
    std::string response;          // exact bytes received
    HttpResponseParser parser;
    uint16_t connId = 0;
    uint16_t localPort = 0;
    bool reused = false;
    bool kept = false;             // connection left open after this request
    int8_t lwipErr = 0;            // err_t that ended the request, 0 = none

    // time_us_64() stamps, 0 = did not happen
    uint64_t tStart = 0, tConnected = 0, tSent = 0, tAcked = 0, tFirstByte = 0, tLastByte = 0, tDone = 0;

    // "GET /path HTTP/1.1" + Host + extra headers (+ Content-Length) + body
    static std::string build(const std::string& method, const std::string& path, const std::string& host,
                             const std::vector<std::string>& headers, const std::string& body);
};

class HttpListener {
public:
    virtual ~HttpListener() = default;
    // may be called from lwIP callbacks: must not call back into HttpClient
    virtual void onHttpEvent(HttpEvent ev, uint16_t reqId, uint16_t connId, int32_t arg,
                             const uint8_t* data, size_t len) = 0;
    // called from HttpClient::poll(); the request is freed afterwards
    virtual void onHttpDone(const HttpRequest& req) = 0;
};

class HttpClient {
public:
    static constexpr size_t kMaxResponse = 16 * 1024;

    explicit HttpClient(HttpListener& listener) : listener_(listener) {}
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    void setTarget(const ip_addr_t& ip, uint16_t port);

    bool busy() const { return cur_ != nullptr || done_ != nullptr; }
    // false if busy (request is dropped)
    bool start(std::unique_ptr<HttpRequest> req);
    // timeouts + delivery of the finished request; call from the main loop
    void poll();

    uint16_t keptConnId() const { return (!cur_ && conn_.pcb) ? conn_.id : 0; }
    void closeKept(ConnPolicy how);
    // fail the current request and drop any connection (e.g. USB link down)
    void abortAll(HttpResult reason);

private:
    struct Conn {
        struct tcp_pcb* pcb = nullptr;
        uint16_t id = 0;
        bool connected = false;
        size_t unacked = 0;
        uint64_t lastUse = 0; // end of the last request on this connection
    };

    void connect();
    err_t send();
    err_t finish(HttpResult result, ConnPolicy closeHow, err_t lwipErr = ERR_OK);
    err_t completeOk();
    err_t release(ConnPolicy how);
    uint16_t curId() const;
    void event(HttpEvent ev, int32_t arg = 0, const uint8_t* data = nullptr, size_t len = 0);
    void note(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void noteHeaders(const HttpRequest& r);

    static err_t onConnected(void* arg, struct tcp_pcb* pcb, err_t err);
    static err_t onRecv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err);
    static err_t onSent(void* arg, struct tcp_pcb* pcb, u16_t len);
    static void onErr(void* arg, err_t err);

    HttpListener& listener_;
    ip_addr_t target_ = IPADDR4_INIT_BYTES(10, 0, 7, 16);
    uint16_t port_ = 80;
    uint16_t nextConnId_ = 1;
    Conn conn_;
    std::unique_ptr<HttpRequest> cur_;
    std::unique_ptr<HttpRequest> done_;
};
