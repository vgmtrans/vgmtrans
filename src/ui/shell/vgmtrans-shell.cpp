/**
 * VGMTrans (c) - 2002-2026
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#ifndef  _WIN32
#include <signal.h>
#endif
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define close _close
#define fileno _fileno
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#endif

#include <fmt/base.h>
#include <fmt/format.h>

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

bool helpRequested(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "-h" || arg == "--help") {
      return true;
    }
  }
  return false;
}

std::string captureStdout(const std::function<void()>& fn) {
  std::cout.flush();
  std::fflush(stdout);

  FILE* tmp = std::tmpfile();
  if (!tmp) {
    fn();
    return {};
  }

  int stdoutFd = fileno(stdout);
  int savedStdout = dup(stdoutFd);

  if (savedStdout == -1) {
    std::fclose(tmp);
    fn();
    return {};
  }

  int tmpFd = fileno(tmp);

  auto restore = [&]() {
    std::cout.flush();
    std::fflush(stdout);
    dup2(savedStdout, stdoutFd);
    close(savedStdout);
  };

  if (dup2(tmpFd, stdoutFd) == -1) {
    close(savedStdout);
    std::fclose(tmp);
    fn();
    return {};
  }

  try {
    fn();
    restore();
  } catch (...) {
    restore();
    std::fclose(tmp);
    throw;
  }

  std::string output;
  std::fseek(tmp, 0, SEEK_SET);

  char buffer[8192];
  while (size_t n = std::fread(buffer, 1, sizeof(buffer), tmp)) {
    output.append(buffer, n);
  }

  std::fclose(tmp);
  return output;
}

bool executableExists(const std::string& cmd) {
#ifdef _WIN32
  // On Windows, try to execute in a hidden way
  std::string checkCmd = cmd + " >nul 2>&1 || (exit /b 1)";
  return std::system(checkCmd.c_str()) == 0;
#else
  // On Unix, use 'command -v' to check if executable exists
  std::string checkCmd = "command -v " + cmd + " >/dev/null 2>&1";
  return std::system(checkCmd.c_str()) == 0;
#endif
}

std::string defaultPager() {
#ifdef _WIN32
  return "more";
#else
  if (executableExists("less")) {
    return "less -R";
  }
  if (executableExists("more")) {
    return "more";
  }
  return {};
#endif
}

std::string selectPager() {
  if (const char* pager = std::getenv("PAGER")) {
    if (*pager != '\0') {
      return pager;
    }
  }

  return defaultPager();
}

void pageLargeOutput(const std::string& output) {
  size_t lineCount = 0;
  for (char c : output) {
    if (c == '\n') lineCount++;
  }

  const size_t OUTPUT_SIZE_THRESHOLD = 3000;  // characters
  const size_t OUTPUT_LINE_THRESHOLD = 40;    // lines

  if (output.size() > OUTPUT_SIZE_THRESHOLD || lineCount > OUTPUT_LINE_THRESHOLD) {
    std::string pager = selectPager();

    if (pager.empty()) {
      fmt::print("{}", output);
      return;
    }

    FILE* pipe = popen(pager.c_str(), "w");
    if (!pipe) {
      fmt::print("{}", output);
      return;
    }

    fwrite(output.c_str(), 1, output.size(), pipe);

    int status = pclose(pipe);
    if (status != 0) {
      fmt::print("{}", output);
    }
  } else {
    fmt::print("{}", output);
  }
}

void printHelp() {
  fmt::println("VGMTrans shell is an interactive command-line interface for inspecting loaded music data and exporting it.");
  fmt::println("");
  cmd_help({});
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
  if (helpRequested(argc, argv)) {
    registerCommands();
    printHelp();
    return 0;
  }

  if (!dbgRoot.init()) {
    fmt::println(stderr, "Failed to init VGMRoot");
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

  fmt::println("Welcome to the VGMTrans shell. Type 'help' for commands.");

  // Auto-load files passed as arguments
  for (int i = 1; i < argc; ++i) {
    std::vector<std::string> loadArgs = {"load", argv[i]};
    cmd_load(loadArgs);
  }

  char* result;
  while (true) {
    result = linenoise("(vgmt) ");

    if (g_sigint_received) {
      g_sigint_received = 0;
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
      if (cmdName == "exit" || cmdName == "quit") {
        break;
      }

      std::string output = captureStdout([&]() {
        if (!it->second.verbs.empty()) {
          dispatchVerb(cmdName, args);
        } else if (cmdName == "help") {
          cmd_help(args);
        } else if (cmdName == "load") {
          cmd_load(args);
        }
      });

      if (!output.empty()) {
        pageLargeOutput(output);
      }
    } else {
      fmt::println("Unknown command. Type 'help'.");
    }
  }

  fmt::println("Goodbye!");
  return 0;
}