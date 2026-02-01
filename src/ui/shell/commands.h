/**
 * VGMTrans (c) - 2002-2026
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

struct Verb {
  std::string name;
  std::string args;
  std::string description;
  int minArgs;  // Minimum total args (including noun and verb)
  std::function<void(const std::vector<std::string>&)> handler;
};

struct Command {
  std::string name;
  std::string description;
  std::vector<Verb> verbs;

  std::string usage() const {
    if (verbs.empty()) {
      return name;
    }
    std::string result = name + " <";
    for (size_t i = 0; i < verbs.size(); ++i) {
      if (i > 0)
        result += "|";
      result += verbs[i].name;
    }
    result += ">";
    return result;
  }

  std::string detailedDescription() const {
    std::string result;
    for (const auto& verb : verbs) {
      result += "    " + verb.name;
      if (!verb.args.empty()) {
        result += " " + verb.args;
      }
      size_t len = 4 + verb.name.length() + (verb.args.empty() ? 0 : 1 + verb.args.length());
      if (len < 40) {
        result += std::string(40 - len, ' ');
      } else {
        result += "  ";
      }
      result += verb.description + "\n";
    }
    if (!result.empty() && result.back() == '\n') {
      result.pop_back();
    }
    return result;
  }

  std::string verbUsage(const std::string& verbName) const {
    for (const auto& verb : verbs) {
      if (verb.name == verbName) {
        return name + " " + verb.name + (verb.args.empty() ? "" : " " + verb.args);
      }
    }
    return name;
  }
};

extern std::map<std::string, Command> commandRegistry;

void registerCommands();
bool dispatchVerb(const std::string& noun, const std::vector<std::string>& args);
