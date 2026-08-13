/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

namespace vgmtrans::core {

class FormatRegistry;
class SequenceDialectRegistry;
class Session;

}  // namespace vgmtrans::core

namespace vgmtrans::formats {

void registerValueFormatModules(core::FormatRegistry& registry);
void registerValueSequenceDialects(core::SequenceDialectRegistry& registry);
void registerValueFormats(core::Session& session);

}  // namespace vgmtrans::formats
