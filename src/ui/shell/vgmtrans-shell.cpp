/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "commands.h"
#include "linenoise.h"
#include "value/formats/ValueFormats.h"
#include "value/session/Session.h"

namespace {

bool terminalInput() {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
  return isatty(fileno(stdin)) && isatty(fileno(stdout));
#endif
}

void complete(const char* input, linenoiseCompletions* completions) {
  for (const auto& command : vgmtrans::shell::completeCommand(input)) {
    linenoiseAddCompletion(completions, command.c_str());
  }
}

std::filesystem::path historyPath() {
#ifdef _WIN32
  if (const char* appdata = std::getenv("APPDATA")) {
    return std::filesystem::path(appdata) / "vgmtrans-shell" / ".vgmtrans-history";
  }
#else
#ifndef __APPLE__
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
    return std::filesystem::path(xdg) / "vgmtrans-shell" / ".vgmtrans-history";
  }
#endif
  if (const char* home = std::getenv("HOME")) {
#ifdef __APPLE__
    return std::filesystem::path(home) / "Library" / "Application Support" / "vgmtrans-shell" / ".vgmtrans-history";
#else
    return std::filesystem::path(home) / ".config" / "vgmtrans-shell" / ".vgmtrans-history";
#endif
  }
#endif
  return {};
}

int run(int argc, char* argv[]) {
  using vgmtrans::shell::CommandResult;
  std::vector<std::string> commands;
  std::vector<std::string> loadArgs{"load"};
  bool pathsOnly = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (!pathsOnly && (arg == "-h" || arg == "--help")) {
      vgmtrans::shell::printHelp(std::cout);
      return 0;
    }
    if (!pathsOnly && arg == "--") {
      pathsOnly = true;
    } else if (!pathsOnly && (arg == "-c" || arg == "--command")) {
      if (++i == argc) {
        throw std::invalid_argument("missing command after " + arg);
      }
      commands.emplace_back(argv[i]);
    } else if (!pathsOnly && arg.starts_with('-')) {
      throw std::invalid_argument("unknown option " + arg + "; use -- before filenames beginning with '-'");
    } else {
      loadArgs.push_back(arg);
    }
  }

  vgmtrans::core::Session session;
  vgmtrans::formats::registerValueFormats(session);
  const bool interactive = commands.empty() && terminalInput();
  if (loadArgs.size() > 1) {
    const auto result = vgmtrans::shell::execute(session, loadArgs, std::cout, std::cerr);
    if (result == CommandResult::Error && !interactive) {
      return 1;
    }
  }
  for (const auto& command : commands) {
    const auto result = vgmtrans::shell::executeLine(session, command, std::cout, std::cerr);
    if (result != CommandResult::Success) {
      return result == CommandResult::Exit ? 0 : 1;
    }
  }
  if (!commands.empty()) {
    return 0;
  }

  if (!interactive) {
    std::string line;
    while (std::getline(std::cin, line)) {
      const auto result = vgmtrans::shell::executeLine(session, line, std::cout, std::cerr);
      if (result != CommandResult::Success) {
        return result == CommandResult::Exit ? 0 : 1;
      }
    }
    if (!std::cin.eof()) {
      throw std::runtime_error("failed to read commands from stdin");
    }
    return 0;
  }

  linenoiseSetCompletionCallback(complete);
  linenoiseHistorySetMaxLen(1000);
  auto history = historyPath();
  if (!history.empty()) {
    std::error_code error;
    std::filesystem::create_directories(history.parent_path(), error);
    if (error) {
      history.clear();
    } else {
      linenoiseHistoryLoad(history.string().c_str());
    }
  }
  std::cout << "VGMTrans shell. Type 'help' for commands.\n";
  while (true) {
    errno = 0;
    const std::unique_ptr<char, decltype(&linenoiseFree)> line(linenoise("(vgmt) "), linenoiseFree);
    if (!line) {
      if (errno == EAGAIN || errno == EINTR) {
        continue;  // linenoise reports Ctrl-C as EAGAIN.
      }
      break;
    }
    if (*line != '\0') {
      linenoiseHistoryAdd(line.get());
      if (!history.empty()) {
        linenoiseHistorySave(history.string().c_str());
      }
    }
    if (vgmtrans::shell::executeLine(session, line.get(), std::cout, std::cerr) == CommandResult::Exit) {
      break;
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
