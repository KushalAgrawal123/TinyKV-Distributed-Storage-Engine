#include "tinykv/protocol/parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace tinykv {

namespace {

constexpr size_t kMaxLineLength = 8192;

std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> tokens;
  std::istringstream iss(line);
  std::string token;
  while (iss >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

std::string toUpper(const std::string& s) {
  std::string result = s;
  std::transform(result.begin(), result.end(), result.begin(),
                  [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return result;
}

CommandType commandTypeFromName(const std::string& name) {
  std::string upper = toUpper(name);
  if (upper == "SET") return CommandType::SET;
  if (upper == "GET") return CommandType::GET;
  if (upper == "DEL") return CommandType::DEL;
  if (upper == "PING") return CommandType::PING;
  if (upper == "INCR") return CommandType::INCR;
  if (upper == "DECR") return CommandType::DECR;
  return CommandType::UNKNOWN;
}

}  // namespace

Command Parser::parse(const std::string& line) {
  if (line.size() > kMaxLineLength) {
    throw ProtocolError("line too long (max " + std::to_string(kMaxLineLength) + " bytes)");
  }

  std::vector<std::string> tokens = tokenize(line);
  if (tokens.empty()) {
    throw ProtocolError("empty command");
  }

  Command cmd;
  cmd.name = tokens[0];
  cmd.type = commandTypeFromName(tokens[0]);
  cmd.args.assign(tokens.begin() + 1, tokens.end());
  return cmd;
}

}  // namespace tinykv
