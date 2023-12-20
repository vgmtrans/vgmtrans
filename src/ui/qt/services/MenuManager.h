/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "common.h"
#include <typeindex>
#include <typeinfo>
#include <QMenu>
#include "Root.h"
#include "util/UIHelpers.h"

#include <ghc/filesystem.hpp>

namespace fs = ghc::filesystem;

class VGMItem;
class VGMFile;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class RawFile;
class CommandSeparator;

/**
 * Represents the type of value a property holds in a CommandContext's PropertyMap.
 */
enum class PropertySpecValueType {
  Path,           ///< Represents a file path or directory path if an ItemList property is present with > 1 items
  DirPath,        ///< Represents a directory path
  ItemList,       ///< Represents a list of items, such as selected files in the VGMFileListView
};

/**
 * A variant type used to store different types of property values in a PropertyMap
 */
using PropertyValue = variant<
    int,
    float,
    string,
    shared_ptr<vector<VGMFile*>>,
    shared_ptr<vector<VGMSeq*>>,
    shared_ptr<vector<VGMInstrSet*>>,
    shared_ptr<vector<VGMSampColl*>>,
    shared_ptr<vector<RawFile*>>
>;

using PropertyMap = map<string, PropertyValue>;

/**
 * The base class for a command context. The command context provides an interface for commands
 * to receive data. CommandContexts are created indirectly by CommandContextFactories via the CreateContext() method.
 */
class CommandContext {
public:
  virtual ~CommandContext() = default;
};

/**
 * PropertySpecifications specify the keys and values types required for the PropertyMap that will
 * be passed into a CommandContextFactory's CreateContext() method. A CommandContextFactory will
 * expose these specifications via the GetPropertySpecifications() method
 */
struct PropertySpecification {
  string key;
  PropertySpecValueType valueType;
  string description;
  PropertyValue defaultValue;
};

using PropertySpecifications = vector<PropertySpecification>;

/**
 * The base class of a factory for creating CommandContext objects. Implementations of this class are responsible
 * for creating a CommandContext that will be passed into a Command's Execute() method. It does this via the
 * CreateContext() method, which expects a PropertyMap. The key and value types required to be set in the PropertyMap
 * are given via the GetPropertySpecifications() method.
 */
class CommandContextFactory {
public:
  virtual ~CommandContextFactory() = default;

  /**
   * Creates a CommandContext object based on the provided properties.
   * @param properties A map of properties to be used for context creation.
   * @return A shared pointer to the created CommandContext object.
   */
  [[nodiscard]] virtual shared_ptr<CommandContext> CreateContext(const PropertyMap& properties) const = 0;

  /**
   * Retrieves the specifications for the properties required to be set in the PropertyMap passed to CreateContext().
   * @return A list of property specifications.
   */
  [[nodiscard]] virtual PropertySpecifications GetPropertySpecifications() const = 0;
};


/**
 * Base class for all commands. A Command is the same thing as a menu action. The GetContextFactory() method
 * provides a factory that can be used to generate the CommandContext required by the Execute method.
 */
class Command {
public:
  virtual ~Command() = default;

  /**
   * Executes the command within the given context.
   * @param context A reference to the CommandContext which will provide the data needed to execute the command.
   */
  virtual void Execute(CommandContext& context) = 0;

  /**
   * Retrieves the name of the command to appear in the UI. Commands must use unique Names, or they will
   * break the behavior of batch command processing (an example of batch processing is
   * selecting multiple sequences and executing the "Save to MIDI" command).
   * @return The name of the command.
   */
  [[nodiscard]] virtual string Name() const = 0;

  /**
   * Gets the factory for creating a command context specific to this command.
   * @return A shared pointer to the CommandContextFactory.
   */
  [[nodiscard]] virtual shared_ptr<CommandContextFactory> GetContextFactory() const = 0;
};


/**
 * MenuManager is the service responsible for the registration and retrieval of commands.
 */
 class MenuManager {
public:
  using CheckFunc = function<bool(VGMItem*)>;
  using CommandsList = vector<shared_ptr<Command>>;
  using CheckedTypeEntry = pair<CheckFunc, CommandsList>;

  MenuManager();

  /**
   * Registers a command for the template parameter type.
   * @param command The command to be registered.
   */
  template<typename T>
  void RegisterCommand(shared_ptr<Command> command);

  /**
   * Registers multiple commands at once for the template parameter type.
   * @param commands A vector of commands to be registered.
   */
  template<typename T>
  void RegisterCommands(const vector<shared_ptr<Command>>& commands);

  /**
   * Retrieves the list of commands suitable for a given item.
   * @param item The VGMItem for which commands are requested.
   * @return A vector of shared pointers to the commands.
   */
  template<typename T>
  vector<std::shared_ptr<Command>> GetCommands(T* item) {
    if (!item) {
      return {}; // Return empty vector if item is nullptr
    }

    std::type_index typeIndex(typeid(*item));

    // Check direct type associations first
    auto it = commandsForType.find(typeIndex);
    if (it != commandsForType.end()) {
      return it->second;
    }

    // Special handling for VGMItem types
    auto vgmItem = dynamic_cast<VGMItem*>(item);
    if (vgmItem) {
      // Check for matches using full class hierarchy
      for (const auto& pair : commandsForCheckedType) {
        if (pair.first(vgmItem)) {
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
 * @param commandManager A reference to a MenuManager
 * @return A list of commands shared by all items, ordered as they are in the first item's Command registry.
   */
  template <typename T>
  vector<shared_ptr<Command>> FindCommonCommands(const vector<T*>& items) {
    if (items.empty()) {
      return {};
    }

    vector<shared_ptr<Command>> commonCommands;
    // Get commands of the first file as the reference order
    auto referenceCommands = GetCommands(items.front());

    // Iterate over each command in the reference list
    for (const auto& refCmd : referenceCommands) {
      string refCmdName = refCmd->Name();
      bool isCommon = true;

      // Check if this command is present in all other files
      for (auto* item : items) {
        auto commands = GetCommands(item);
        if (none_of(commands.begin(), commands.end(),
                    [&refCmdName](const shared_ptr<Command>& cmd) { return cmd->Name() == refCmdName; })) {
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

  template <typename T>
  QMenu* CreateMenuForItems(shared_ptr<vector<T*>> selectedFiles) {

    auto menu = new QMenu();
    if (selectedFiles->empty()) {
      return nullptr;
    }
    auto commands = FindCommonCommands(*selectedFiles);

    for (const auto& command : commands) {

      // Handle CommandSeparator
      if (dynamic_cast<CommandSeparator*>(command.get()) != nullptr) {
        menu->addSeparator();
        continue;
      }

      auto contextFactory = command->GetContextFactory();
      auto propSpecs = contextFactory->GetPropertySpecifications();

      menu->addAction(command->Name().c_str(), [this, command, selectedFiles, propSpecs, contextFactory] {
        PropertyMap propMap;
        for (const auto& propSpec : propSpecs) {

          bool isDirPath = false;
          switch (propSpec.valueType) {
            case PropertySpecValueType::DirPath:
              isDirPath = true;
            case PropertySpecValueType::Path:
              if (isDirPath || selectedFiles->size() > 1) {
                fs::path dirpath = fs::path(OpenSaveDirDialog());
                if (dirpath.string().empty()) {
                  return;
                }
                propMap.insert({ propSpec.key, dirpath.generic_string() });
              } else {
                auto suggestedFileName = ConvertToSafeFileName(*(*selectedFiles)[0]->GetName());
                auto fileExtension = get<string>(propSpec.defaultValue);
                auto path = OpenSaveFileDialog(suggestedFileName, string2wstring(fileExtension));
                if (path.empty()) {
                  return;
                }
                propMap.insert({ propSpec.key, path });
              }
              break;
            case PropertySpecValueType::ItemList:
              propMap.insert({ propSpec.key, selectedFiles });
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

private:
  map<type_index, vector<shared_ptr<Command>>> commandsForType;
  vector<CheckedTypeEntry> commandsForCheckedType;
};
