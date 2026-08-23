#include "tinykv/command_executor.hpp"

#include "tinykv/protocol/reply.hpp"

namespace tinykv {

namespace {

std::string arityError(const Command& cmd) {
  return Reply::error("wrong number of arguments for '" + cmd.name + "'");
}

}  // namespace

CommandExecutor::CommandExecutor(KVStore& store) : store_(store) {}

std::string CommandExecutor::execute(const Command& cmd) {
  switch (cmd.type) {
    case CommandType::SET: {
      if (cmd.args.size() != 2) return arityError(cmd);
      store_.set(cmd.args[0], cmd.args[1]);
      return Reply::ok();
    }
    case CommandType::GET: {
      if (cmd.args.size() != 1) return arityError(cmd);
      auto value = store_.get(cmd.args[0]);
      return value.has_value() ? Reply::bulk(*value) : Reply::nil();
    }
    case CommandType::DEL: {
      if (cmd.args.size() != 1) return arityError(cmd);
      return Reply::integer(store_.del(cmd.args[0]) ? 1 : 0);
    }
    case CommandType::PING: {
      if (!cmd.args.empty()) return arityError(cmd);
      return Reply::ok();
    }
    case CommandType::UNKNOWN:
    default:
      return Reply::error("unknown command '" + cmd.name + "'");
  }
}

}  // namespace tinykv
