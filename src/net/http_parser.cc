#include "http_parser.h"

#include <cctype>
#include <cstdlib>

static bool ieq(const std::string& s, size_t pos, size_t end, const char* lit) {
    size_t i = 0;
    for (; lit[i]; ++i) {
        if (pos + i >= end) return false;
        if (std::tolower((unsigned char)s[pos + i]) != lit[i]) return false;
    }
    return true;
}

// case-insensitive search of a lowercase token in s[pos, end)
static bool icontains(const std::string& s, size_t pos, size_t end, const char* tok) {
    for (size_t p = pos; p < end; ++p) {
        if (ieq(s, p, end, tok)) return true;
    }
    return false;
}

bool HttpResponseParser::parseHeaders(const std::string& buf) {
    // reject early if this is clearly not HTTP
    if (buf.size() >= 5 && buf.compare(0, 5, "HTTP/") != 0) return false;

    size_t end = buf.find("\r\n\r\n");
    if (end == std::string::npos) return true; // need more data

    // status line: HTTP/1.1 200 OK
    size_t eol = buf.find("\r\n");
    if (eol < 12 || buf.compare(0, 7, "HTTP/1.") != 0 || !std::isdigit((unsigned char)buf[7]) || buf[8] != ' ') {
        return false;
    }
    minor_ = buf[7] - '0';
    status_ = std::atoi(buf.c_str() + 9);
    if (status_ < 100 || status_ > 999) return false;

    // header lines
    size_t line = eol + 2;
    while (line < end) {
        size_t le = buf.find("\r\n", line);
        size_t colon = buf.find(':', line);
        if (colon == std::string::npos || colon > le) return false;
        size_t v = colon + 1;
        while (v < le && (buf[v] == ' ' || buf[v] == '\t')) ++v;

        if (ieq(buf, line, colon, "content-length") && colon - line == 14) {
            char* stop = nullptr;
            long n = std::strtol(buf.c_str() + v, &stop, 10);
            if (stop == buf.c_str() + v || n < 0) return false;
            contentLength_ = n;
        } else if (ieq(buf, line, colon, "transfer-encoding") && colon - line == 17) {
            chunked_ = icontains(buf, v, le, "chunked");
        } else if (ieq(buf, line, colon, "connection") && colon - line == 10) {
            connClose_ = icontains(buf, v, le, "close");
        }
        line = le + 2;
    }
    if (minor_ == 0) connClose_ = true; // HTTP/1.0 default (keep-alive ignored)

    headerEnd_ = end + 4;
    chunkPos_ = headerEnd_;
    state_ = State::Body;
    return true;
}

// walks chunk-size lines and chunk data; returns false on a framing error
bool HttpResponseParser::advanceChunked(const std::string& buf) {
    for (;;) {
        if (chunkState_ == Chunk::Size) {
            size_t le = buf.find("\r\n", chunkPos_);
            if (le == std::string::npos) return true;
            char* stop = nullptr;
            unsigned long n = std::strtoul(buf.c_str() + chunkPos_, &stop, 16);
            if (stop == buf.c_str() + chunkPos_) return false;
            chunkPos_ = le + 2;
            if (n == 0) {
                chunkState_ = Chunk::Trailer;
            } else {
                chunkState_ = Chunk::Data;
                chunkPos_ += n; // data, CRLF checked below
            }
        } else if (chunkState_ == Chunk::Data) {
            if (buf.size() < chunkPos_ + 2) return true;
            if (buf.compare(chunkPos_, 2, "\r\n") != 0) return false;
            chunkPos_ += 2;
            chunkState_ = Chunk::Size;
        } else { // Trailer: header lines until an empty line
            size_t le = buf.find("\r\n", chunkPos_);
            if (le == std::string::npos) return true;
            if (le == chunkPos_) {
                complete(CompleteBy::Chunked);
                return true;
            }
            chunkPos_ = le + 2;
        }
    }
}

HttpResponseParser::State HttpResponseParser::update(const std::string& buf) {
    if (state_ == State::Headers) {
        if (!parseHeaders(buf)) {
            state_ = State::Error;
            return state_;
        }
    }
    if (state_ != State::Body) return state_;

    if (noBody_ || status_ == 204 || status_ == 304) {
        complete(CompleteBy::NoBody);
    } else if (chunked_) {
        if (!advanceChunked(buf)) state_ = State::Error;
    } else if (contentLength_ >= 0) {
        if (buf.size() - headerEnd_ >= (size_t)contentLength_) complete(CompleteBy::ContentLength);
    }
    // else: no framing info, wait for FIN
    return state_;
}

HttpResponseParser::State HttpResponseParser::peerClosed(const std::string& buf) {
    update(buf);
    if (state_ == State::Body && !chunked_ && contentLength_ < 0) {
        complete(CompleteBy::PeerClose);
    }
    return state_;
}
