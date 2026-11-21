// Incremental HTTP/1.x response framing parser. Hardware independent
// (unit-tested on the host, see test/).
//
// It only decides *when a response is complete* and whether the connection
// may be reused; the bytes themselves stay in the caller's buffer.
// Handles: headers split across segments, Content-Length, no body (204/304),
// Transfer-Encoding: chunked (incl. trailers), and "read until close".

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class HttpResponseParser {
public:
    enum class State : uint8_t { Headers, Body, Complete, Error };
    enum class CompleteBy : uint8_t { None = 0, ContentLength = 1, NoBody = 2, Chunked = 3, PeerClose = 4 };

    void reset() { *this = HttpResponseParser(); }
    // response to HEAD: headers only, whatever Content-Length says
    void expectNoBody() { noBody_ = true; }

    // call after appending data to buf (buf always holds the whole response)
    State update(const std::string& buf);
    // call when the peer closed the connection (FIN)
    State peerClosed(const std::string& buf);

    State state() const { return state_; }
    CompleteBy completeBy() const { return by_; }

    int status() const { return status_; }          // -1 until the status line is parsed
    int httpMinor() const { return minor_; }        // HTTP/1.<minor>
    size_t headerEnd() const { return headerEnd_; } // index of the first body byte, npos until known
    long contentLength() const { return contentLength_; } // -1 = absent
    bool chunked() const { return chunked_; }
    bool connectionClose() const { return connClose_; }

    // true if the response was framed (not ended by FIN) and the server did
    // not ask to close the connection
    bool canKeepAlive() const {
        return state_ == State::Complete && by_ != CompleteBy::PeerClose && !connClose_ && minor_ >= 1;
    }

private:
    bool parseHeaders(const std::string& buf);
    bool advanceChunked(const std::string& buf);
    void complete(CompleteBy by) { state_ = State::Complete; by_ = by; }

    State state_ = State::Headers;
    CompleteBy by_ = CompleteBy::None;
    int status_ = -1;
    int minor_ = -1;
    size_t headerEnd_ = std::string::npos;
    long contentLength_ = -1;
    bool chunked_ = false;
    bool connClose_ = false;
    bool noBody_ = false;

    // chunked state
    enum class Chunk : uint8_t { Size, Data, Trailer } chunkState_ = Chunk::Size;
    size_t chunkPos_ = 0;   // absolute index into buf
};
