/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <filesystem>
#include <optional>
#include <typeindex>
#include <variant>
#include <vector>
#include <map>
#include <qkeysequence.h>

class VGMFile;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class RawFile;
class VGMColl;

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
using PropertyValue = std::variant<
    int,
    float,
    std::string,
    std::shared_ptr<std::vector<VGMFile*>>,
    std::shared_ptr<std::vector<VGMSeq*>>,
    std::shared_ptr<std::vector<VGMInstrSet*>>,
    std::shared_ptr<std::vector<VGMSampColl*>>,
    std::shared_ptr<std::vector<RawFile*>>,
    std::shared_ptr<std::vector<VGMColl*>>
>;

using PropertyMap = std::map<std::string, PropertyValue>;

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
  std::string key;
  PropertySpecValueType valueType;
  std::string description;
  PropertyValue defaultValue;
};

using PropertySpecifications = std::vector<PropertySpecification>;

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
  [[nodiscard]] virtual std::shared_ptr<CommandContext> createContext(const PropertyMap& properties) const = 0;

  /**
   * Retrieves the specifications for the properties required to be set in the PropertyMap passed to CreateContext().
   * @return A list of property specifications.
   */
  [[nodiscard]] virtual PropertySpecifications propertySpecifications() const = 0;
};


/**
 * Base class for all commands. A Command is the same thing as a menu action. The GetContextFactory() method
 * provides a factory that can be used to generate the CommandContext required by the Execute method.
 */
class Command {
public:
  using MenuPath = std::vector<std::string>;

  virtual ~Command() = default;

  /**
   * Executes the command within the given context.
   * @param context A reference to the CommandContext which will provide the data needed to execute the command.
   */
  virtual void execute(CommandContext& context) = 0;

  /**
   * Retrieves the name of the command to appear in the UI. Commands must use unique Names, or they will
   * break the behavior of batch command processing (an example of batch processing is
   * selecting multiple sequences and executing the "Save to MIDI" command).
   * @return The name of the command.
   */
  [[nodiscard]] virtual std::string name() const = 0;

  /**
   * Gets the factory for creating a command context specific to this command.
   * @return A shared pointer to the CommandContextFactory.
   */
  [[nodiscard]] virtual std::shared_ptr<CommandContextFactory> contextFactory() const = 0;

  /**
   * Defines a key sequence that will be displayed in the menu as a shortcut for the command
   * @return the keyboard shortcut
   */
  [[nodiscard]] virtual QKeySequence shortcutKeySequence() const { return -1; };

  /**
   * Specifies the menu path where this command should be surfaced in the main menu bar.
   * Returning std::nullopt means the command is contextual only.
   * The first element of the path represents the top-level menu name.
   */
  [[nodiscard]] virtual std::optional<MenuPath> menuPath() const { return std::nullopt; }
};

class CommandSeparator : public Command {
public:
  CommandSeparator() = default;

  void execute(CommandContext&) override {}

  [[nodiscard]] std::shared_ptr<CommandContextFactory> contextFactory() const override { return nullptr; }
  [[nodiscard]] std::string name() const override { return "separator"; }
};

namespace MenuPaths {
inline const Command::MenuPath File{"File"};
inline const Command::MenuPath Convert{"Convert"};
inline const Command::MenuPath Preview{"Preview"};
}
