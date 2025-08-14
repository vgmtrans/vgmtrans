/*
* VGMTrans (c) 2002-2025
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "MdiArea.h"
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
 * A base Command class for simple batchable commands. Receives a vector of pointers to the instances
 */
template <typename T>
class ItemListCommand : public Command {
public:
  ItemListCommand()
      : m_contextFactory(std::make_shared<ItemListContextFactory<T>>()) {}

  void execute(CommandContext& context) override {
    auto& vgmContext = dynamic_cast<ItemListCommandContext<T>&>(context);
    const auto& items = vgmContext.items();

    for (auto item : items) {
      auto specificItem = dynamic_cast<T*>(item);
      executeItem(specificItem);
    }
  }

  [[nodiscard]] std::shared_ptr<CommandContextFactory> contextFactory() const override {
    return m_contextFactory;
  }

  virtual void executeItem(T* item) const = 0;

private:
  std::shared_ptr<ItemListContextFactory<T>> m_contextFactory;
};

/**
 * A base Command class for single item commands. Works identically to ItemListCommand, but only
 * executes the first item of an item list
 */
template <typename T>
class SingleItemCommand : public ItemListCommand<T> {
public:
  void execute(CommandContext& context) override {
    auto& vgmContext = dynamic_cast<ItemListCommandContext<T>&>(context);
    T* item = vgmContext.items().front();
    this->executeItem(item);
  }
};

/**
 * A command for closing a VGMFile
 */
class CloseVGMFileCommand : public ItemListCommand<VGMFile> {
public:
  void executeItem(VGMFile* file) const override {
    pRoot->removeVGMFile(vgmFileToVariant(file));
  }
  [[nodiscard]] QKeySequence shortcutKeySequence() const override { return Qt::Key_Backspace; };
  [[nodiscard]] std::string name() const override { return "Remove"; }
};

/**
 * A command for closing a RawFile
 */
class CloseRawFileCommand : public ItemListCommand<RawFile> {
public:
  void executeItem(RawFile* file) const override {
    pRoot->closeRawFile(file);
  }
  [[nodiscard]] QKeySequence shortcutKeySequence() const override { return Qt::Key_Backspace; };
  [[nodiscard]] std::string name() const override { return "Close"; }
};

/**
 * A command for closing a VGMColl
 */
class CloseVGMCollCommand : public ItemListCommand<VGMColl> {
public:
  void executeItem(VGMColl* coll) const override {
    pRoot->removeVGMColl(coll);
  }
  [[nodiscard]] QKeySequence shortcutKeySequence() const override { return Qt::Key_Backspace; };
  [[nodiscard]] std::string name() const override { return "Remove"; }
};

/**
 * A command for opening a VGMFile in the MdiArea
 */
class OpenCommand : public ItemListCommand<VGMFile> {
public:
  void executeItem(VGMFile* file) const override {
    MdiArea::the()->newView(file);
  }
  [[nodiscard]] QKeySequence shortcutKeySequence() const override { return Qt::Key_Return; };
  [[nodiscard]] std::string name() const override { return "Open"; }
};