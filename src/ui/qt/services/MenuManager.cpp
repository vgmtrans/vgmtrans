/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "MenuManager.h"
#include "services/commands/GeneralCommands.h"
#include "services/commands/SaveCommands.h"

MenuManager::MenuManager() {
  RegisterCommands<VGMSeq, VGMItem>({
      std::make_shared<SaveAsMidiCommand>(),
      std::make_shared<SaveAsOriginalFormatCommand<VGMFile>>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<CloseVGMFileCommand>(),
  });
  RegisterCommands<VGMInstrSet, VGMItem>({
      std::make_shared<SaveAsSF2Command>(),
      std::make_shared<SaveAsDLSCommand>(),
      std::make_shared<SaveAsOriginalFormatCommand<VGMFile>>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<CloseVGMFileCommand>(),
  });
  RegisterCommands<VGMSampColl, VGMItem>({
      std::make_shared<SaveWavBatchCommand>(),
      std::make_shared<SaveAsOriginalFormatCommand<VGMFile>>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<CloseVGMFileCommand>(),
  });
  RegisterCommands<VGMFile, VGMItem>({
      std::make_shared<CloseVGMFileCommand>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<SaveAsOriginalFormatCommand<VGMFile>>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<CloseVGMFileCommand>(),
  });

  RegisterCommands<RawFile>({
      std::make_shared<SaveAsOriginalFormatCommand<RawFile>>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<CloseRawFileCommand>(),
  });

  RegisterCommands<VGMColl>({
      std::make_shared<SaveCollCommand<conversion::Target::MIDI | conversion::Target::SF2>>(),
      std::make_shared<SaveCollCommand<conversion::Target::MIDI | conversion::Target::DLS>>(),
      std::make_shared<SaveCollCommand<conversion::Target::MIDI | conversion::Target::SF2
                       | conversion::Target::DLS>>(),
  });
}

template<typename T, typename Base>
void MenuManager::RegisterCommands(const std::vector<std::shared_ptr<Command>>& commands) {
  commandsForType[typeid(T)] = commands;

  if constexpr (std::is_convertible_v<T*, Base*>) {
    auto checkFunc = [](void* base) -> bool { return dynamic_cast<T*>(static_cast<Base*>(base)) != nullptr; };
    auto typeErasedCheckFunc = static_cast<CheckFunc<void>>(checkFunc);
    commandsForCheckedType[typeid(Base)].emplace_back(typeErasedCheckFunc, commands);
  }
}
