/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "services/MenuManager.h"
#include "VGMFile.h"

class CommandSeparator : public Command {
public:
  CommandSeparator() = default;

  void Execute(CommandContext&) override {}

  [[nodiscard]] std::shared_ptr<CommandContextFactory> GetContextFactory() const override { return nullptr; }
  [[nodiscard]] std::string Name() const override { return "separator"; }
};

/**
 * A command context that provides a vector of pointers
 */
template <typename T>
class ItemListCommandContext : public CommandContext {
private:
  std::shared_ptr<std::vector<T*>> items{};

public:
  ItemListCommandContext() = default;

  void SetItems(std::shared_ptr<std::vector<T*>> f) {
    items = f;
  }

  [[nodiscard]] const std::vector<T*>& GetItems() const { return *items; }
};

/**
 * A command context factory to construct a context providing a vector of pointers
 */
template <typename T>
class ItemListContextFactory : public CommandContextFactory {
public:
  explicit ItemListContextFactory() : CommandContextFactory() {}

  [[nodiscard]] std::shared_ptr<CommandContext> CreateContext(const PropertyMap& properties) const override {
    auto context = std::make_shared<ItemListCommandContext<T>>();

    if (!properties.contains("items")) {
      return nullptr;
    }

    auto files = get<std::shared_ptr<std::vector<T*>>>(properties.at("items"));
    context->SetItems(files);
    return context;
  }

  [[nodiscard]] PropertySpecifications GetPropertySpecifications() const override {
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
private:
  std::shared_ptr<ItemListContextFactory<TClosable>> contextFactory;

public:
  CloseCommand()
      : contextFactory(std::make_shared<ItemListContextFactory<TClosable>>()) {}

  void Execute(CommandContext& context) override {
    auto& vgmContext = dynamic_cast<ItemListCommandContext<TClosable>&>(context);
    const auto& files = vgmContext.GetItems();

    for (auto file : files) {
      auto specificFile = dynamic_cast<TClosable*>(file);
      Close(specificFile);
    }
  }

  [[nodiscard]] std::shared_ptr<CommandContextFactory> GetContextFactory() const override {
    return contextFactory;
  }

  virtual void Close(TClosable* specificFile) const = 0;
  [[nodiscard]] std::string Name() const override { return "Close"; }
};

/**
 * A command for closing a VGMFile
 */
class CloseVGMFileCommand : public CloseCommand<VGMFile> {
public:
  CloseVGMFileCommand() : CloseCommand<VGMFile>() {}

  void Close(VGMFile* file) const override {
    g_root->RemoveVGMFile(vgmFileToVariant(file));
  }
};

/**
 * A command for closing a RawFile
 */
class CloseRawFileCommand : public CloseCommand<RawFile> {
public:
  CloseRawFileCommand() : CloseCommand<RawFile>() {}

  void Close(RawFile* file) const override {
    g_root->CloseRawFile(file);
  }
};