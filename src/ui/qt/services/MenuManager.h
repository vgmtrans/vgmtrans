/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "services/commands/Command.h"
#include <filesystem>
#include <QMenu>
#include "common.h"
#include "Root.h"
#include "UIHelpers.h"
#include "LogManager.h"
#include "helper.h"

namespace fs = std::filesystem;

/**
 * MenuManager is the service responsible for the registration and retrieval of commands.
 */
class MenuManager {
public:
  template<typename T, typename Base = T>
  using CheckFunc = std::function<bool(Base*)>;
  using CommandsList = std::vector<std::shared_ptr<Command>>;
  using MenuPath = Command::MenuPath;
  using MenuCommandMap = std::map<MenuPath, CommandsList>;
  template<typename T>
  using CheckedTypeEntry = std::pair<CheckFunc<T>, CommandsList>;

  static auto the() {
    static MenuManager* instance = new MenuManager();
    return instance;
  }

  MenuManager(const MenuManager&) = delete;
  MenuManager&operator=(const MenuManager&) = delete;
  MenuManager(MenuManager&&) = delete;
  MenuManager&operator=(MenuManager&&) = delete;

private:
  std::map<std::type_index, std::vector<std::shared_ptr<Command>>> commandsForType;
  std::map<std::type_index, std::vector<CheckedTypeEntry<void>>> commandsForCheckedType;

public:
  MenuManager();

  /**
   * Registers multiple commands at once for the template parameter type.
   * @param commands A vector of commands to be registered.
   */
  template<typename T, typename Base = T>
  void registerCommands(const std::vector<std::shared_ptr<Command>>& commands);

  /**
   * Retrieves the list of commands suitable for a given item.
   * @param base The object instance for which commands are requested.
   * @return A vector of shared pointers to the commands.
   */
  template<typename Base>
  std::vector<std::shared_ptr<Command>> getCommands(Base* base) {
    if (!base) {
      return {}; // Return empty vector if base is nullptr
    }

    std::type_index typeIndex(typeid(*base));

    // Check direct type associations first
    auto it = commandsForType.find(typeIndex);
    if (it != commandsForType.end()) {
      return it->second;
    }

    // Check for matches using full class hierarchy
    auto baseTypeIndex = std::type_index(typeid(Base));
    auto baseIt = commandsForCheckedType.find(baseTypeIndex);
    if (baseIt != commandsForCheckedType.end()) {
      for (const auto& pair : baseIt->second) {
        if (pair.first(base)) {
          return pair.second;
        }
      }
    }

    // Return empty vector if no commands are found
    return {};
  }

  /**
 * Given a list of objects, returns the list of Commands shared by all the instance types, as registered in the
 * MenuManager.
 * @param items The list of objects for which to find common Commands
 * @return A list of commands shared by all items, ordered as they are in the first item's Command registry.
   */
  template <typename Base, typename T>
  std::vector<std::shared_ptr<Command>> findCommonCommands(const std::vector<T*>& items) {
    if (items.empty()) {
      return {};
    }

    std::vector<std::shared_ptr<Command>> commonCommands;
    // Get commands of the first file as the reference order
    auto referenceCommands = getCommands<Base>(items.front());

    // Iterate over each command in the reference list
    for (const auto& refCmd : referenceCommands) {
      std::string refCmdName = refCmd->name();
      bool isCommon = true;

      // Check if this command is present in all other files
      for (auto* item : items) {
        auto commands = getCommands<Base>(item);
        if (none_of(commands.begin(), commands.end(),
                    [&refCmdName](const std::shared_ptr<Command>& cmd) { return cmd->name() == refCmdName; })) {
          isCommon = false;
          break;
        }
      }

      if (isCommon) {
        commonCommands.push_back(refCmd);
      }
    }
    return commonCommands;
  }


  // Helper trait to determine if Base has a GetName method.
  template <typename Base, typename = void>
  struct has_getname : std::false_type {};

  template <typename Base>
  struct has_getname<Base, decltype(std::declval<Base>().name(), void())> : std::true_type {};

  /**
 * Create a QMenu populated with actions for the commands common to all of the instances in the provided vector
 * @param items The list of objects the actions of the QMenu should pertain to
 * @return A QMenu populated with actions for the common commands, in the same order as they were registered for the first item
   */
  template <typename Base, typename T = Base>
  QMenu* createMenuForItems(std::shared_ptr<std::vector<T*>> items) {

    auto menu = new QMenu();
    if (items->empty()) {
      return nullptr;
    }
    auto commands = findCommonCommands<Base>(*items);
    createActionsForCommands<Base>(commands, items, menu, true);
    return menu;
  }

  template <typename Base, typename T = Base>
  MenuCommandMap commandsByMenuForItems(std::shared_ptr<std::vector<T*>> items) {
    MenuCommandMap groupedCommands;
    if (!items || items->empty()) {
      return groupedCommands;
    }

    auto commands = findCommonCommands<Base>(*items);
    for (const auto& command : commands) {
      auto path = command->menuPath();
      if (!path || path->empty()) {
        continue;
      }
      groupedCommands[*path].push_back(command);
    }

    return groupedCommands;
  }

  template <typename Base, typename T = Base>
  std::vector<QAction*> createActionsForCommands(const std::vector<std::shared_ptr<Command>>& commands,
                                                 std::shared_ptr<std::vector<T*>> items,
                                                 QMenu* menu,
                                                 bool includeSeparators = false) {
    std::vector<QAction*> createdActions;

    for (const auto& command : commands) {
      if (includeSeparators) {
        if (dynamic_cast<CommandSeparator*>(command.get()) != nullptr) {
          createdActions.push_back(menu->addSeparator());
          continue;
        }
      } else if (dynamic_cast<CommandSeparator*>(command.get()) != nullptr) {
        continue;
      }

      auto contextFactory = command->contextFactory();
      if (!contextFactory) {
        continue;
      }
      auto propSpecs = contextFactory->propertySpecifications();

      auto action = menu->addAction(command->name().c_str(), [command, items, propSpecs, contextFactory] {
        PropertyMap propMap;
        for (const auto& propSpec : propSpecs) {

          bool isDirPath = false;
          switch (propSpec.valueType) {
            case PropertySpecValueType::DirPath:
              isDirPath = true;
            case PropertySpecValueType::Path:
              if (isDirPath || items->size() > 1) {
                fs::path dirpath = openSaveDirDialog();
                if (dirpath.empty()) {
                  return;
                }
                propMap.insert({ propSpec.key, dirpath });
              } else {
                std::filesystem::path suggestedFileName;
                if constexpr (has_getname<T>::value) {
                  suggestedFileName = makeSafeFileName((*items)[0]->name());
                }
                auto fileExtension = get<std::filesystem::path>(propSpec.defaultValue);
                auto path = openSaveFileDialog(suggestedFileName, fileExtension.string());
                if (path.empty()) {
                  return;
                }
                propMap.insert({propSpec.key, path});
              }
              break;
            case PropertySpecValueType::ItemList:
              propMap.insert({ propSpec.key, items });
              break;
            default:
              L_ERROR("Unknown PropertySpecValueType value: {}", static_cast<int>(propSpec.valueType));
              break;
          }
        }
        auto context = contextFactory->createContext(propMap);
        if (!context) {
          return;
        }
        command->execute(*context);
      });
      if (const auto& keySequences = command->shortcutKeySequences(); !keySequences.empty()) {
        action->setShortcuts(keySequences);
        action->setShortcutVisibleInContextMenu(true);
      }
      createdActions.push_back(action);
    }

    return createdActions;
  }
};
