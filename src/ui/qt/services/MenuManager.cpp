/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "MenuManager.h"
#include "services/commands/GeneralCommands.h"
#include "services/commands/SaveCommands.h"

MenuManager::MenuManager() {
  RegisterCommands<VGMSeq>({
      make_shared<CloseVGMFileCommand>(),
      make_shared<CommandSeparator>(),
      make_shared<SaveAsMidiCommand>(),
      make_shared<SaveAsOriginalFormatCommand>(),
  });
  RegisterCommands<VGMInstrSet>({
      make_shared<CloseVGMFileCommand>(),
      make_shared<CommandSeparator>(),
      make_shared<SaveAsSF2Command>(),
      make_shared<SaveAsDLSCommand>(),
      make_shared<SaveAsOriginalFormatCommand>(),
  });
  RegisterCommands<VGMSampColl>({
      make_shared<CloseVGMFileCommand>(),
      make_shared<CommandSeparator>(),
      make_shared<SaveWavBatchCommand>(),
      make_shared<SaveAsOriginalFormatCommand>(),
  });
  RegisterCommands<VGMFile>({
      make_shared<CloseVGMFileCommand>(),
      make_shared<CommandSeparator>(),
      make_shared<SaveAsOriginalFormatCommand>(),
  });

  RegisterCommands<RawFile>({
      make_shared<CloseVGMFileCommand>(),
  });
}

template<typename T>
void MenuManager::RegisterCommand(shared_ptr<Command> command) {
  commandsForType[typeid(T)].push_back(command);

  if constexpr (is_convertible<T*, VGMItem*>::value) {
    auto checkFunc = [](VGMItem* item) -> bool { return dynamic_cast<T*>(item) != nullptr; };
    auto it = find_if(
        commandsForCheckedType.begin(), commandsForCheckedType.end(),
        [&checkFunc](const CheckedTypeEntry& entry) { return &entry.first == &checkFunc; });

    if (it != commandsForCheckedType.end()) {
      // Add command to the existing entry
      it->second.push_back(command);
    } else {
      // Create a new entry
      commandsForCheckedType.emplace_back(checkFunc, CommandsList{command});
    }
  }
}

template<typename T>
void MenuManager::RegisterCommands(const vector<shared_ptr<Command>>& commands) {
  commandsForType[typeid(T)] = commands;

  if constexpr (is_convertible<T*, VGMItem*>::value) {
    auto checkFunc = [](VGMItem* item) -> bool { return dynamic_cast<T*>(item) != nullptr; };
    commandsForCheckedType.emplace_back(checkFunc, commands);
  }
}
