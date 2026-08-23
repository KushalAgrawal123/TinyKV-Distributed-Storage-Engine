#pragma once

#include <string>

namespace tinykv {

// Builds wire-formatted TinyKV protocol reply lines (see docs/PROTOCOL.md).
// Each function returns the reply payload only, WITHOUT the trailing '\n'
// terminator - that's added by the transport that writes it out (see
// writeLine in net/line_protocol.hpp).
namespace Reply {

std::string ok();
std::string bulk(const std::string& value);
std::string nil();
std::string error(const std::string& message);
std::string integer(long long value);

}  // namespace Reply
}  // namespace tinykv
