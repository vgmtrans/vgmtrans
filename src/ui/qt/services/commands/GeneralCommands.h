/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "services/commands/Command.h"
#include "VGMFile.h"

/**
 * A command context that provides a vector of pointers
 */
template <typename T>
class ItemListCommandContext : public CommandContext {
public:
  ItemListCommandContext() = default;

  void setItems(std::shared_ptr<std::vector<T*>> f) {
    m_items = f;
  }

  [[nodiscard]] const std::vector<T*>& items() const { return *m_items; }

private:
  std::shared_ptr<std::vector<T*>> m_items{};
};

/**
 * A command context factory to construct a context providing a vector of pointers
 */
template <typename T>
class ItemListContextFactory : public CommandContextFactory {
public:
  explicit ItemListContextFactory() : CommandContextFactory() {}

  [[nodiscard]] std::shared_ptr<CommandContext> createContext(const PropertyMap& properties) const override {
    auto context = std::make_shared<ItemListCommandContext<T>>();

    if (!properties.contains("items")) {
      return nullptr;
    }

    auto files = get<std::shared_ptr<std::vector<T*>>>(properties.at("items"));
    context->setItems(files);
    return context;
  }

  [[nodiscard]] PropertySpecifications propertySpecifications() const override {
    return {
        {"items", PropertySpecValueType::ItemList, "Vector of files",
          static_cast<std::shared_ptr<std::vector<T*>>>(nullptr)},
    };
  }
};

/**
 * The base Command class for a "Close" command. Receives a vector of pointers to the instances to close
 */
template <typename TClosable>
class CloseCommand : public Command {
public:
  CloseCommand()
      : m_contextFactory(std::make_shared<ItemListContextFactory<TClosable>>()) {}

  void execute(CommandContext& context) override {
    auto& vgmContext = dynamic_cast<ItemListCommandContext<TClosable>&>(context);
    const auto& files = vgmContext.items();

    for (auto file : files) {
      auto specificFile = dynamic_cast<TClosable*>(file);
      close(specificFile);
    }
  }

  [[nodiscard]] std::shared_ptr<CommandContextFactory> contextFactory() const override {
    return m_contextFactory;
  }

  virtual void close(TClosable* specificFile) const = 0;
  [[nodiscard]] std::string name() const override { return "Close"; }

private:
  std::shared_ptr<ItemListContextFactory<TClosable>> m_contextFactory;
};

/**
 * A command for closing a VGMFile
 */
class CloseVGMFileCommand : public CloseCommand<VGMFile> {
public:
  CloseVGMFileCommand() : CloseCommand<VGMFile>() {}

  void close(VGMFile* file) const override {
    pRoot->removeVGMFile(vgmFileToVariant(file));
  }
};

/**
 * A command for closing a RawFile
 */
class CloseRawFileCommand : public CloseCommand<RawFile> {
public:
  CloseRawFileCommand() : CloseCommand<RawFile>() {}

  void close(RawFile* file) const override {
    pRoot->closeRawFile(file);
  }
};