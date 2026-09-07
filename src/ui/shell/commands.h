/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::core {
class Session;
}

namespace vgmtrans::shell {

enum class CommandResult { Success, Error, Exit };

// Commands borrow the session and output streams. All workspace state belongs
// to Session; the terminal and scripts use exactly the same command path.
[[nodiscard]] CommandResult execute(core::Session& session, std::span<const std::string> args, std::ostream& output,
                                    std::ostream& errors);
[[nodiscard]] CommandResult executeLine(core::Session& session, std::string_view line, std::ostream& output,
                                        std::ostream& errors);
void printHelp(std::ostream& output);
[[nodiscard]] std::vector<std::string> completeCommand(std::string_view prefix);

}  // namespace vgmtrans::shell
