// Tiny JSON value lookup for the camera's small, flat responses. Finds the
// first occurrence of "key" and parses the value after the colon. Good enough
// for e.g. {"iris": ...} or the first entry of media/workingset; not a parser.
#pragma once

#include <cstddef>
#include <string>

namespace json {

bool number(const std::string& body, const char* key, double& out);
bool boolean(const std::string& body, const char* key, bool& out);
bool string(const std::string& body, const char* key, std::string& out);
// whole body is a bare value: `true` / `false`
bool bareBool(const std::string& body, bool& out);

} // namespace json
