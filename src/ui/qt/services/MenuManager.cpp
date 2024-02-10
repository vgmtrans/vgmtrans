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
      make_shared<CloseVGMFileCommand>(),
      make_shared<CommandSeparator>(),
      make_shared<SaveAsMidiCommand>(),
      make_shared<SaveAsOriginalFormatCommand>(),
  });
  RegisterCommands<VGMInstrSet, VGMItem>({
      make_shared<CloseVGMFileCommand>(),
      make_shared<CommandSeparator>(),
      make_shared<SaveAsSF2Command>(),
      make_shared<SaveAsDLSCommand>(),
      make_shared<SaveAsOriginalFormatCommand>(),
  });
  RegisterCommands<CPS1OPMInstrSet, VGMItem>({
      make_shared<CloseVGMFileCommand>(),
      make_shared<CommandSeparator>(),
      make_shared<SaveAsOPMCommand>(),
      make_shared<SaveAsOriginalFormatCommand>(),
  });
  RegisterCommands<VGMSampColl, VGMItem>({
      make_shared<CloseVGMFileCommand>(),
      make_shared<CommandSeparator>(),
      make_shared<SaveWavBatchCommand>(),
      make_shared<SaveAsOriginalFormatCommand>(),
  });
  RegisterCommands<VGMFile, VGMItem>({
      make_shared<CloseVGMFileCommand>(),
      make_shared<CommandSeparator>(),
      make_shared<SaveAsOriginalFormatCommand>(),
  });

  RegisterCommands<RawFile>({
      make_shared<CloseVGMFileCommand>(),
  });
}

template<typename T, typename Base>
void MenuManager::RegisterCommands(const vector<shared_ptr<Command>>& commands) {
  commandsForType[typeid(T)] = commands;

  if constexpr (is_convertible<T*, Base*>::value) {
    auto checkFunc = [](void* base) -> bool { return dynamic_cast<T*>(static_cast<Base*>(base)) != nullptr; };
    auto typeErasedCheckFunc = static_cast<CheckFunc<void>>(checkFunc);
    commandsForCheckedType[typeid(Base)].emplace_back(typeErasedCheckFunc, commands);
  }
}
