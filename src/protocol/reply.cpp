#include "tinykv/protocol/reply.hpp"

namespace tinykv {
namespace Reply {

std::string ok() { return "+OK"; }

std::string bulk(const std::string& value) { return "+" + value; }

std::string nil() { return "$-1"; }

std::string error(const std::string& message) { return "-ERR " + message; }

std::string readOnlyError() { return "-READONLY You can't write against a replica."; }

std::string integer(long long value) { return ":" + std::to_string(value); }

}  // namespace Reply
}  // namespace tinykv
