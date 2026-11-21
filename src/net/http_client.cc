#include "http_client.h"

extern "C" {
#include <pico/time.h>
}

#include <cstdarg>
#include <cstdio>

std::string HttpRequest::build(const std::string& method, const std::string& path, const std::string& host,
                               const std::vector<std::string>& headers, const std::string& body) {
    std::string r;
    r.reserve(128 + path.size() + body.size());
    r += method;
    r += ' ';
    r += path;
    r += " HTTP/1.1\r\n";
    if (!host.empty()) {
        r += "Host: ";
        r += host;
        r += "\r\n";
    }
    for (const auto& h : headers) {
        r += h;
        r += "\r\n";
    }
    // PUT/POST/PATCH always carry a length, even for an empty body
    if (!body.empty() || method == "PUT" || method == "POST" || method == "PATCH") {
        r += "Content-Length: ";
        r += std::to_string(body.size());
        r += "\r\n";
    }
    r += "\r\n";
    r += body; // exactly Content-Length bytes, nothing appended
    return r;
}

/* ------------------------------------------------------------------------ */

HttpClient::~HttpClient() {
    abortAll(HttpResult::LinkDown);
}

void HttpClient::setTarget(const ip_addr_t& ip, uint16_t port) {
    if (!ip_addr_cmp(&ip, &target_) || port != port_) {
        closeKept(ConnPolicy::Close); // kept connection points to the old target
    }
    target_ = ip;
    port_ = port;
}

uint16_t HttpClient::curId() const {
    return cur_ ? cur_->id : 0;
}

void HttpClient::event(HttpEvent ev, int32_t arg, const uint8_t* data, size_t len) {
    listener_.onHttpEvent(ev, curId(), conn_.id, arg, data, len);
}

void HttpClient::note(const char* fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(buf)) n = sizeof(buf) - 1;
    event(HttpEvent::Note, 0, (const uint8_t*)buf, (size_t)n);
}

void HttpClient::noteHeaders(const HttpRequest& r) {
    const HttpResponseParser& p = r.parser;
    note("headers: HTTP/1.%d %d, %u B, content-length %ld%s%s", p.httpMinor(), p.status(),
         (unsigned)p.headerEnd(), p.contentLength(), p.chunked() ? ", chunked" : "",
         p.connectionClose() ? ", connection close" : "");
}

bool HttpClient::start(std::unique_ptr<HttpRequest> req) {
    if (busy()) return false;
    cur_ = std::move(req);
    cur_->tStart = time_us_64();
    cur_->parser.reset();
    if (cur_->head) cur_->parser.expectNoBody();

    if (conn_.pcb && cur_->newConnection) {
        note("new-conn requested: closing kept conn %u", conn_.id);
        release(ConnPolicy::Close);
    }

    if (conn_.pcb) {
        cur_->reused = true;
        cur_->connId = conn_.id;
        cur_->localPort = conn_.pcb->local_port;
        cur_->tConnected = cur_->tStart;
        event(HttpEvent::Reused);
        note("reusing conn %u, idle %lu ms", conn_.id, (unsigned long)((cur_->tStart - conn_.lastUse) / 1000));
        send(); // on failure the request is finished inside
    } else {
        connect();
    }
    return true;
}

void HttpClient::connect() {
    struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        // MEMP_NUM_TCP_PCB exhausted (lwIP already tried to recycle TIME_WAIT)
        note("tcp_new failed: no free pcb");
        finish(HttpResult::ConnectFailed, ConnPolicy::Abort, ERR_MEM);
        return;
    }
    conn_ = Conn();
    conn_.pcb = pcb;
    conn_.id = nextConnId_++;
    if (nextConnId_ == 0) nextConnId_ = 1;
    cur_->connId = conn_.id;

    tcp_arg(pcb, this);
    tcp_recv(pcb, onRecv);
    tcp_sent(pcb, onSent);
    tcp_err(pcb, onErr);

    err_t err = tcp_connect(pcb, &target_, port_, onConnected);
    cur_->localPort = pcb->local_port;
    event(HttpEvent::Connecting, pcb->local_port);
    if (err != ERR_OK) {
        note("tcp_connect err %d", err);
        finish(HttpResult::ConnectFailed, ConnPolicy::Close, err); // close frees a not-connected pcb
    }
}

// returns ERR_ABRT if the pcb was aborted
err_t HttpClient::send() {
    HttpRequest& r = *cur_;
    struct tcp_pcb* pcb = conn_.pcb;
    if (r.bytes.size() > tcp_sndbuf(pcb)) {
        note("request %u B > sndbuf %u B", (unsigned)r.bytes.size(), (unsigned)tcp_sndbuf(pcb));
        return finish(HttpResult::WriteFailed, ConnPolicy::Abort, ERR_MEM);
    }
    err_t err = tcp_write(pcb, r.bytes.data(), (u16_t)r.bytes.size(), TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) err = tcp_output(pcb);
    if (err != ERR_OK) {
        note("tcp_write/tcp_output err %d", err);
        return finish(HttpResult::WriteFailed, ConnPolicy::Abort, err);
    }
    conn_.unacked = r.bytes.size();
    r.tSent = time_us_64();
    return ERR_OK;
}

// Detaches and closes/aborts the current pcb. Returns ERR_ABRT if aborted.
err_t HttpClient::release(ConnPolicy how) {
    struct tcp_pcb* pcb = conn_.pcb;
    if (!pcb) return ERR_OK;
    conn_.pcb = nullptr;
    conn_.connected = false;

    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr); // lwIP default: tcp_recv_null
    tcp_sent(pcb, nullptr);
    tcp_err(pcb, nullptr);
    tcp_poll(pcb, nullptr, 0);

    if (how != ConnPolicy::Abort) {
        err_t err = tcp_close(pcb);
        if (err == ERR_OK) {
            event(HttpEvent::Close);
            return ERR_OK;
        }
        // ERR_MEM: no room to enqueue the FIN -> abort instead
        note("tcp_close err %d -> abort", err);
    }
    tcp_abort(pcb);
    event(HttpEvent::Abort);
    return ERR_ABRT;
}

err_t HttpClient::finish(HttpResult result, ConnPolicy closeHow, err_t lwipErr) {
    if (!cur_) return ERR_OK;
    HttpRequest& r = *cur_;
    r.result = result;
    r.lwipErr = (int8_t)lwipErr;
    err_t ret = release(closeHow);
    r.tDone = time_us_64();
    done_ = std::move(cur_);
    return ret;
}

err_t HttpClient::completeOk() {
    HttpRequest& r = *cur_;
    event(HttpEvent::ResponseComplete, (int32_t)r.parser.completeBy());
    if (r.parser.completeBy() == HttpResponseParser::CompleteBy::ContentLength) {
        size_t extra = r.response.size() - r.parser.headerEnd() - (size_t)r.parser.contentLength();
        if (extra) note("%u bytes beyond content-length", (unsigned)extra);
    }
    if (r.policy == ConnPolicy::Keep && !r.parser.canKeepAlive()) {
        note("keep refused: %s", r.parser.connectionClose() ? "connection close / HTTP/1.0" : "response not length framed");
    }
    if (r.policy == ConnPolicy::Keep && r.parser.canKeepAlive() && conn_.pcb) {
        conn_.lastUse = time_us_64();
        r.kept = true;
        r.result = HttpResult::Ok;
        r.tDone = time_us_64();
        done_ = std::move(cur_);
        return ERR_OK;
    }
    // Keep requested but the response cannot be reused -> plain close
    return finish(HttpResult::Ok, r.policy == ConnPolicy::Abort ? ConnPolicy::Abort : ConnPolicy::Close);
}

void HttpClient::poll() {
    if (cur_ && time_us_64() - cur_->tStart > (uint64_t)cur_->timeoutMs * 1000) {
        event(HttpEvent::Timeout);
        note("timeout after %lu ms, %u B received, parser state %d", (unsigned long)cur_->timeoutMs,
             (unsigned)cur_->response.size(), (int)cur_->parser.state());
        finish(HttpResult::Timeout, ConnPolicy::Abort); // outside lwIP callbacks: abort is safe
    }
    if (done_) {
        std::unique_ptr<HttpRequest> d = std::move(done_);
        listener_.onHttpDone(*d);
    }
}

void HttpClient::closeKept(ConnPolicy how) {
    if (!cur_ && conn_.pcb) release(how);
}

void HttpClient::abortAll(HttpResult reason) {
    if (cur_) {
        finish(reason, ConnPolicy::Abort);
    } else {
        release(ConnPolicy::Abort);
    }
}

/* lwIP callbacks --------------------------------------------------------- */

err_t HttpClient::onConnected(void* arg, struct tcp_pcb* pcb, err_t err) {
    auto* self = static_cast<HttpClient*>(arg);
    if (!self || pcb != self->conn_.pcb || !self->cur_) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    if (err != ERR_OK) {
        return self->finish(HttpResult::ConnectFailed, ConnPolicy::Abort, err);
    }
    self->conn_.connected = true;
    self->cur_->tConnected = time_us_64();
    self->event(HttpEvent::Connected);
    return self->send();
}

err_t HttpClient::onSent(void* arg, struct tcp_pcb* pcb, u16_t len) {
    auto* self = static_cast<HttpClient*>(arg);
    if (!self || pcb != self->conn_.pcb) return ERR_OK;
    Conn& c = self->conn_;
    if (c.unacked == 0) return ERR_OK;
    c.unacked = len >= c.unacked ? 0 : c.unacked - len;
    if (c.unacked == 0 && self->cur_) {
        self->cur_->tAcked = time_us_64();
        self->event(HttpEvent::Acked, (int32_t)self->cur_->bytes.size());
    }
    return ERR_OK;
}

err_t HttpClient::onRecv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    auto* self = static_cast<HttpClient*>(arg);
    if (!self || pcb != self->conn_.pcb) { // stale pcb: should not happen (detached on release)
        if (p) {
            tcp_recved(pcb, p->tot_len);
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if (!p) {
        // FIN from the camera
        self->event(HttpEvent::PeerFin);
        if (!self->cur_) {
            return self->release(ConnPolicy::Close); // idle kept connection
        }
        HttpRequest& r = *self->cur_;
        if (r.parser.peerClosed(r.response) == HttpResponseParser::State::Complete) {
            self->event(HttpEvent::ResponseComplete, (int32_t)r.parser.completeBy());
            return self->finish(HttpResult::Ok, ConnPolicy::Close);
        }
        self->note("FIN before end of response: %u B, parser state %d", (unsigned)r.response.size(),
                   (int)r.parser.state());
        return self->finish(HttpResult::ClosedEarly, ConnPolicy::Close);
    }

    if (err != ERR_OK) { // not used by lwIP 2.1, handled defensively
        pbuf_free(p);
        return self->finish(HttpResult::Reset, ConnPolicy::Abort, err);
    }

    const u16_t len = p->tot_len;
    tcp_recved(pcb, len);

    if (!self->cur_) {
        // data on an idle kept connection: report it, drop it
        uint8_t tmp[256];
        u16_t n = pbuf_copy_partial(p, tmp, sizeof(tmp), 0);
        pbuf_free(p);
        self->event(HttpEvent::IdleData, len, tmp, n);
        return ERR_OK;
    }

    HttpRequest& r = *self->cur_;
    const uint64_t now = time_us_64();
    if (!r.tFirstByte) r.tFirstByte = now;
    r.tLastByte = now;
    self->event(HttpEvent::Data, len);

    if (r.response.size() + len > kMaxResponse) {
        self->note("response > %u B", (unsigned)kMaxResponse);
        pbuf_free(p);
        return self->finish(HttpResult::TooLarge, ConnPolicy::Abort);
    }
    for (struct pbuf* q = p; q; q = q->next) {
        r.response.append(static_cast<const char*>(q->payload), q->len);
    }
    pbuf_free(p);

    const bool hadHeaders = r.parser.state() != HttpResponseParser::State::Headers;
    const HttpResponseParser::State st = r.parser.update(r.response);
    if (!hadHeaders && (st == HttpResponseParser::State::Body || st == HttpResponseParser::State::Complete)) {
        self->noteHeaders(r);
    }
    switch (st) {
        case HttpResponseParser::State::Error:
            self->note("malformed response (%u B)", (unsigned)r.response.size());
            return self->finish(HttpResult::Malformed, ConnPolicy::Abort);
        case HttpResponseParser::State::Complete:
            return self->completeOk();
        default:
            return ERR_OK;
    }
}

void HttpClient::onErr(void* arg, err_t err) {
    // lwIP has already freed the pcb
    auto* self = static_cast<HttpClient*>(arg);
    if (!self) return;
    const bool wasConnected = self->conn_.connected;
    self->conn_.pcb = nullptr;
    self->conn_.connected = false;
    self->event(HttpEvent::Err, err);
    if (self->cur_) {
        self->finish(wasConnected ? HttpResult::Reset : HttpResult::ConnectFailed, ConnPolicy::Abort, err);
    }
}
