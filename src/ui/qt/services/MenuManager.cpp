/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "MenuManager.h"
#include "services/commands/GeneralCommands.h"
#include "services/commands/SaveCommands.h"

MenuManager::MenuManager() {
  registerCommands<VGMSeq, VGMItem>({
      std::make_shared<SaveAsMidiCommand>(),
      std::make_shared<SaveAsOriginalFormatCommand<VGMFile>>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<CloseVGMFileCommand>(),
  });
  registerCommands<VGMInstrSet, VGMItem>({
      std::make_shared<SaveAsSF2Command>(),
      std::make_shared<SaveAsDLSCommand>(),
      std::make_shared<SaveAsOriginalFormatCommand<VGMFile>>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<CloseVGMFileCommand>(),
  });
  registerCommands<CPS1OPMInstrSet, VGMItem>({
      std::make_shared<CloseVGMFileCommand>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<SaveAsOPMCommand>(),
      std::make_shared<SaveAsOriginalFormatCommand<VGMFile>>(),
  });
  registerCommands<VGMSampColl, VGMItem>({
      std::make_shared<SaveWavBatchCommand>(),
      std::make_shared<SaveAsOriginalFormatCommand<VGMFile>>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<CloseVGMFileCommand>(),
  });
  registerCommands<VGMFile, VGMItem>({
      std::make_shared<CloseVGMFileCommand>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<SaveAsOriginalFormatCommand<VGMFile>>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<CloseVGMFileCommand>(),
  });

  registerCommands<RawFile>({
      std::make_shared<SaveAsOriginalFormatCommand<RawFile>>(),
      std::make_shared<CommandSeparator>(),
      std::make_shared<CloseRawFileCommand>(),
  });

  registerCommands<VGMColl>({
      std::make_shared<SaveCollCommand<conversion::Target::MIDI | conversion::Target::SF2>>(),
      std::make_shared<SaveCollCommand<conversion::Target::MIDI | conversion::Target::DLS>>(),
      std::make_shared<SaveCollCommand<conversion::Target::MIDI | conversion::Target::SF2
                       | conversion::Target::DLS>>(),
  });
}

template<typename T, typename Base>
void MenuManager::registerCommands(const std::vector<std::shared_ptr<Command>>& commands) {
  commandsForType[typeid(T)] = commands;

  if constexpr (std::is_convertible_v<T*, Base*>) {
    auto checkFunc = [](void* base) -> bool { return dynamic_cast<T*>(static_cast<Base*>(base)) != nullptr; };
    auto typeErasedCheckFunc = static_cast<CheckFunc<void>>(checkFunc);
    commandsForCheckedType[typeid(Base)].emplace_back(typeErasedCheckFunc, commands);
  }
}
