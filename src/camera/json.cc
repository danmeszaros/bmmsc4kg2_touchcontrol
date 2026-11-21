#include "json.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace json {

static size_t skipWs(const std::string& s, size_t i) {
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return i;
}

// index of the first character of the value of "key", npos if absent
static size_t valueOf(const std::string& body, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    size_t pos = 0;
    while ((pos = body.find(pat, pos)) != std::string::npos) {
        size_t i = skipWs(body, pos + pat.size());
        if (i < body.size() && body[i] == ':') return skipWs(body, i + 1);
        pos += pat.size(); // matched a string value, not a key
    }
    return std::string::npos;
}

bool number(const std::string& body, const char* key, double& out) {
    size_t i = valueOf(body, key);
    if (i == std::string::npos) return false;
    const char* start = body.c_str() + i;
    char* end = nullptr;
    double v = std::strtod(start, &end);
    if (end == start) return false;
    out = v;
    return true;
}

static bool parseBool(const std::string& s, size_t i, bool& out) {
    if (s.compare(i, 4, "true") == 0) { out = true; return true; }
    if (s.compare(i, 5, "false") == 0) { out = false; return true; }
    return false;
}

bool boolean(const std::string& body, const char* key, bool& out) {
    size_t i = valueOf(body, key);
    return i != std::string::npos && parseBool(body, i, out);
}

bool string(const std::string& body, const char* key, std::string& out) {
    size_t i = valueOf(body, key);
    if (i == std::string::npos || body[i] != '"') return false;
    std::string v;
    for (size_t j = i + 1; j < body.size(); ++j) {
        char c = body[j];
        if (c == '"') { out = v; return true; }
        if (c == '\\' && j + 1 < body.size()) c = body[++j];
        v += c;
    }
    return false;
}

bool bareBool(const std::string& body, bool& out) {
    return parseBool(body, skipWs(body, 0), out);
}

} // namespace json
