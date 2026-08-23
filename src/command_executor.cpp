#include "tinykv/command_executor.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>

#include "tinykv/protocol/reply.hpp"

namespace tinykv {

namespace {

std::string arityError(const Command& cmd) {
  return Reply::error("wrong number of arguments for '" + cmd.name + "'");
}

std::string incrementReply(KVStore& store, const std::string& key, long long delta) {
  try {
    return Reply::integer(store.incrementBy(key, delta));
  } catch (const std::invalid_argument&) {
    return Reply::error("value is not an integer or out of range");
  }
}

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

// Parses a strictly positive integer (digits only, no sign). Used for
// SET's EX option and EXPIRE's seconds argument - zero/negative TTLs
// aren't supported (see docs/PROTOCOL.md).
bool parsePositiveSeconds(const std::string& s, long long& outSeconds) {
  if (s.empty() || !std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
    return false;
  }
  try {
    long long value = std::stoll(s);
    if (value <= 0) return false;
    outSeconds = value;
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

CommandExecutor::CommandExecutor(KVStore& store, ExpiryManager& expiry) : store_(store), expiry_(expiry) {}

std::string CommandExecutor::execute(const Command& cmd) {
  ++totalCommands_;

  switch (cmd.type) {
    case CommandType::SET: {
      if (cmd.args.size() == 2) {
        store_.set(cmd.args[0], cmd.args[1]);
        expiry_.cancel(cmd.args[0]);  // a plain SET clears any previous TTL
        return Reply::ok();
      }
      if (cmd.args.size() == 4 && equalsIgnoreCase(cmd.args[2], "EX")) {
        long long seconds = 0;
        if (!parsePositiveSeconds(cmd.args[3], seconds)) {
          return Reply::error("invalid expire time in 'SET' command");
        }
        store_.set(cmd.args[0], cmd.args[1]);
        expiry_.schedule(cmd.args[0], std::chrono::steady_clock::now() + std::chrono::seconds(seconds));
        return Reply::ok();
      }
      return arityError(cmd);
    }
    case CommandType::GET: {
      if (cmd.args.size() != 1) return arityError(cmd);
      auto value = store_.get(cmd.args[0]);
      if (value.has_value()) {
        ++totalHits_;
        return Reply::bulk(*value);
      }
      ++totalMisses_;
      return Reply::nil();
    }
    case CommandType::DEL: {
      if (cmd.args.size() != 1) return arityError(cmd);
      bool existed = store_.del(cmd.args[0]);
      expiry_.cancel(cmd.args[0]);
      return Reply::integer(existed ? 1 : 0);
    }
    case CommandType::PING: {
      if (!cmd.args.empty()) return arityError(cmd);
      return Reply::ok();
    }
    case CommandType::INCR: {
      if (cmd.args.size() != 1) return arityError(cmd);
      return incrementReply(store_, cmd.args[0], 1);
    }
    case CommandType::DECR: {
      if (cmd.args.size() != 1) return arityError(cmd);
      return incrementReply(store_, cmd.args[0], -1);
    }
    case CommandType::TTL: {
      if (cmd.args.size() != 1) return arityError(cmd);
      const std::string& key = cmd.args[0];
      if (!store_.exists(key)) return Reply::integer(-2);
      auto expiresAt = expiry_.expiryOf(key);
      if (!expiresAt.has_value()) return Reply::integer(-1);
      auto remaining =
          std::chrono::duration_cast<std::chrono::seconds>(*expiresAt - std::chrono::steady_clock::now()).count();
      return Reply::integer(remaining > 0 ? remaining : 0);
    }
    case CommandType::EXPIRE: {
      if (cmd.args.size() != 2) return arityError(cmd);
      const std::string& key = cmd.args[0];
      if (!store_.exists(key)) return Reply::integer(0);
      long long seconds = 0;
      if (!parsePositiveSeconds(cmd.args[1], seconds)) {
        return Reply::error("value is not an integer or out of range");
      }
      expiry_.schedule(key, std::chrono::steady_clock::now() + std::chrono::seconds(seconds));
      return Reply::integer(1);
    }
    case CommandType::PERSIST: {
      if (cmd.args.size() != 1) return arityError(cmd);
      const std::string& key = cmd.args[0];
      if (!store_.exists(key)) return Reply::integer(0);
      bool hadExpiry = expiry_.expiryOf(key).has_value();
      expiry_.cancel(key);
      return Reply::integer(hadExpiry ? 1 : 0);
    }
    case CommandType::UNKNOWN:
    default:
      return Reply::error("unknown command '" + cmd.name + "'");
  }
}

}  // namespace tinykv
