/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "services/commands/Command.h"
#include <filesystem>
#include <typeindex>
#include <typeinfo>
#include <QMenu>
#include "common.h"
#include "Root.h"
#include "UIHelpers.h"
#include "LogManager.h"

namespace fs = std::filesystem;

/**
 * MenuManager is the service responsible for the registration and retrieval of commands.
 */
class MenuManager {
public:
  template<typename T, typename Base = T>
  using CheckFunc = std::function<bool(Base*)>;
  using CommandsList = std::vector<std::shared_ptr<Command>>;
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
  void RegisterCommands(const std::vector<std::shared_ptr<Command>>& commands);

  /**
   * Retrieves the list of commands suitable for a given item.
   * @param base The object instance for which commands are requested.
   * @return A vector of shared pointers to the commands.
   */
  template<typename Base>
  std::vector<std::shared_ptr<Command>> GetCommands(Base* base) {
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
  std::vector<std::shared_ptr<Command>> FindCommonCommands(const std::vector<T*>& items) {
    if (items.empty()) {
      return {};
    }

    std::vector<std::shared_ptr<Command>> commonCommands;
    // Get commands of the first file as the reference order
    auto referenceCommands = GetCommands<Base>(items.front());

    // Iterate over each command in the reference list
    for (const auto& refCmd : referenceCommands) {
      std::string refCmdName = refCmd->Name();
      bool isCommon = true;

      // Check if this command is present in all other files
      for (auto* item : items) {
        auto commands = GetCommands<Base>(item);
        if (none_of(commands.begin(), commands.end(),
                    [&refCmdName](const std::shared_ptr<Command>& cmd) { return cmd->Name() == refCmdName; })) {
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
  QMenu* CreateMenuForItems(std::shared_ptr<std::vector<T*>> items) {

    auto menu = new QMenu();
    if (items->empty()) {
      return nullptr;
    }
    auto commands = FindCommonCommands<Base>(*items);

    for (const auto& command : commands) {

      // Handle CommandSeparator
      if (dynamic_cast<CommandSeparator*>(command.get()) != nullptr) {
        menu->addSeparator();
        continue;
      }

      auto contextFactory = command->GetContextFactory();
      auto propSpecs = contextFactory->GetPropertySpecifications();

      menu->addAction(command->Name().c_str(), [command, items, propSpecs, contextFactory] {
        PropertyMap propMap;
        for (const auto& propSpec : propSpecs) {

          bool isDirPath = false;
          switch (propSpec.valueType) {
            case PropertySpecValueType::DirPath:
              isDirPath = true;
            case PropertySpecValueType::Path:
              if (isDirPath || items->size() > 1) {
                fs::path dirpath = fs::path(OpenSaveDirDialog());
                if (dirpath.string().empty()) {
                  return;
                }
                propMap.insert({ propSpec.key, dirpath.generic_string() });
              } else {
                std::string suggestedFileName;
                if constexpr (has_getname<T>::value) {
                  suggestedFileName = ConvertToSafeFileName((*items)[0]->name());
                }
                auto fileExtension = get<std::string>(propSpec.defaultValue);
                auto path = OpenSaveFileDialog(suggestedFileName, fileExtension);
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
        auto context = contextFactory->CreateContext(propMap);
        if (!context) {
          return;
        }
        command->Execute(*context);
      });
    }
    return menu;
  }
};
