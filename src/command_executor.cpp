#include "tinykv/command_executor.hpp"

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

}  // namespace

CommandExecutor::CommandExecutor(KVStore& store) : store_(store) {}

std::string CommandExecutor::execute(const Command& cmd) {
  ++totalCommands_;

  switch (cmd.type) {
    case CommandType::SET: {
      if (cmd.args.size() != 2) return arityError(cmd);
      store_.set(cmd.args[0], cmd.args[1]);
      return Reply::ok();
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
      return Reply::integer(store_.del(cmd.args[0]) ? 1 : 0);
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
    case CommandType::UNKNOWN:
    default:
      return Reply::error("unknown command '" + cmd.name + "'");
  }
}

}  // namespace tinykv
