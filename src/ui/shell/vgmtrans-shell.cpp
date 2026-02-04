/**
 * VGMTrans (c) - 2002-2026
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <cstdio>
#ifndef  _WIN32
#include <signal.h>
#endif

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include "DBGVGMRoot.h"
#include "commands.h"
#include "linenoise.h"

// Forward declarations for control commands in commands.cpp
void cmd_help(const std::vector<std::string>&);
void cmd_load(const std::vector<std::string>&);

volatile sig_atomic_t g_sigint_received = 0;

void sigintHandler(int) {
  g_sigint_received = 1;
}

std::vector<std::string> tokenize(const std::string& input) {
  std::vector<std::string> tokens;
  std::string token;
  char quoteChar = 0;

  for (char c : input) {
    if (quoteChar) {
      if (c == quoteChar) {
        quoteChar = 0;
      } else {
        token += c;
      }
    } else {
      if (c == '"' || c == '\'') {
        quoteChar = c;
      } else if (c == ' ') {
        if (!token.empty()) {
          tokens.push_back(token);
          token.clear();
        }
      } else {
        token += c;
      }
    }
  }
  if (!token.empty()) {
    tokens.push_back(token);
  }
  return tokens;
}

void completionHook(const char* buf, linenoiseCompletions* lc) {
  std::string input(buf);

  size_t firstSpace = input.find(' ');
  if (firstSpace == std::string::npos) {
    // No space: completing command name
    for (const auto& [name, cmd] : commandRegistry) {
      if (name.rfind(input, 0) == 0) {
        linenoiseAddCompletion(lc, name.c_str());
      }
    }
  } else {
    // Has space: get command and partial verb
    std::string cmdName = input.substr(0, firstSpace);
    std::string rest = input.substr(firstSpace + 1);

    // Skip if already past verb (contains another space in rest)
    if (rest.find(' ') != std::string::npos) {
      return;
    }

    auto it = commandRegistry.find(cmdName);
    if (it != commandRegistry.end()) {
      for (const auto& verb : it->second.verbs) {
        if (verb.name.rfind(rest, 0) == 0) {
          std::string completion = cmdName + " " + verb.name;
          linenoiseAddCompletion(lc, completion.c_str());
        }
      }
    }
  }
}

std::filesystem::path configDirectory(const std::string& app_name) {
#ifdef _WIN32
  if (const char* appdata = std::getenv("APPDATA")) {
    return std::filesystem::path(appdata) / app_name;
  }
#elif defined(__APPLE__)
  if (const char* home = std::getenv("HOME")) {
    return std::filesystem::path(home) / "Library" / "Application Support" / app_name;
  }
#else  // Linux / BSD
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
    return std::filesystem::path(xdg) / app_name;
  }
  if (const char* home = std::getenv("HOME")) {
    return std::filesystem::path(home) / ".config" / app_name;
  }
#endif

  // last-resort fallback (current directory)
  return std::filesystem::current_path() / ("." + app_name);
}

int main(int argc, char* argv[]) {
  if (!dbgRoot.init()) {
    std::cerr << "Failed to init VGMRoot" << std::endl;
    return 1;
  }

#ifndef _WIN32
  struct sigaction sa;
  sa.sa_handler = sigintHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
#endif

  registerCommands();
  linenoiseSetCompletionCallback(completionHook);
  linenoiseHistorySetMaxLen(1000);

  auto historyFile = configDirectory("vgmtrans-shell") / ".vgmtrans-history";
  std::filesystem::create_directories(historyFile.parent_path());
  linenoiseHistoryLoad(historyFile.string().c_str());

  std::cout << "Welcome to the VGMTrans shell. Type 'help' for commands." << std::endl;

  char* result;
  while (true) {
    result = linenoise("(vgmt) ");

    if (g_sigint_received) {
      g_sigint_received = 0;
      std::cout << "^C" << std::endl;
      if (result)
        linenoiseFree(result);
      continue;
    }

    if (result == nullptr) {
      break;  // EOF or error
    }

    std::string line = result;

    if (!line.empty()) {
      linenoiseHistoryAdd(result);
      if (!historyFile.empty()) {
        linenoiseHistorySave(historyFile.string().c_str());
      }
    }

    linenoiseFree(result);

    if (line.empty())
      continue;

    auto args = tokenize(line);
    if (args.empty())
      continue;

    std::string cmdName = args[0];

    auto it = commandRegistry.find(cmdName);
    if (it != commandRegistry.end()) {
      // Commands with verbs use dispatchVerb, control commands have direct handlers
      if (!it->second.verbs.empty()) {
        dispatchVerb(cmdName, args);
      } else if (cmdName == "help") {
        cmd_help(args);
      } else if (cmdName == "exit" || cmdName == "quit") {
        break;
      } else if (cmdName == "load") {
        cmd_load(args);
      }
    } else {
      std::cout << "Unknown command. Type 'help'." << std::endl;
    }
  }

  return 0;
}