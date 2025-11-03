/*
* VGMTrans (c) 2002-2025
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "MdiArea.h"
#include "services/commands/Command.h"
#include "VGMFile.h"
#include "helper.h"

using MenuPath = Command::MenuPath;

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

    executeItems(items);
  }

  [[nodiscard]] std::shared_ptr<CommandContextFactory> contextFactory() const override {
    return m_contextFactory;
  }

  virtual void executeItems(std::vector<T*> items) const = 0;

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
  void executeItems(std::vector<T*> items) const override {
    T* item = items.front();
    this->executeItem(item);
  }
  virtual void executeItem(T* item) const = 0;
};

/**
 * A command for closing a VGMFile
 */
class CloseVGMFileCommand : public ItemListCommand<VGMFile> {
public:
  void executeItems(std::vector<VGMFile*> vgmfiles) const override {
    // If all files are selected, we can remove everything at once more efficiently
    if (vgmfiles.size() == pRoot->vgmFiles().size()) {
      pRoot->removeAllFilesAndCollections();
    } else {
      pRoot->UI_beginRemoveAll();
      for (auto vgmfile : vgmfiles) {
        pRoot->removeVGMFile(vgmFileToVariant(vgmfile));
      }
      pRoot->UI_endRemoveAll();
    }
  }
  [[nodiscard]] QList<QKeySequence> shortcutKeySequences() const override { return {Qt::Key_Backspace, Qt::Key_Delete}; };
  [[nodiscard]] std::string name() const override { return "Remove"; }
  [[nodiscard]] std::optional<MenuPath> menuPath() const override { return MenuPaths::File; }
};

/**
 * A command for closing a RawFile
 */
class CloseRawFileCommand : public ItemListCommand<RawFile> {
public:
  void executeItems(std::vector<RawFile*> rawfiles) const override {
    // If all files are selected, we can remove everything at once more efficiently
    if (rawfiles.size() == pRoot->rawFiles().size()) {
      pRoot->removeAllFilesAndCollections();
    } else {
      pRoot->UI_beginRemoveAll();
      for (auto rawfile : rawfiles) {
        pRoot->removeRawFile(rawfile);
      }
      pRoot->UI_endRemoveAll();
    }
  }
  [[nodiscard]] QList<QKeySequence> shortcutKeySequences() const override { return {Qt::Key_Backspace, Qt::Key_Delete}; };
  [[nodiscard]] std::string name() const override { return "Close"; }
  [[nodiscard]] std::optional<MenuPath> menuPath() const override { return MenuPaths::File; }
};

/**
 * A command for closing a VGMColl
 */
class CloseVGMCollCommand : public ItemListCommand<VGMColl> {
public:
  void executeItems(std::vector<VGMColl*> vgmcolls) const override {
    pRoot->UI_beginRemoveVGMColls();
    for (auto coll : vgmcolls) {
      pRoot->removeVGMColl(coll);
    }
    pRoot->UI_endRemoveVGMColls();
  }
  [[nodiscard]] QList<QKeySequence> shortcutKeySequences() const override { return {Qt::Key_Backspace, Qt::Key_Delete}; };
  [[nodiscard]] std::string name() const override { return "Remove"; }
  [[nodiscard]] std::optional<MenuPath> menuPath() const override { return MenuPaths::File; }
};

/**
 * A command for opening a VGMFile in the MdiArea
 */
class OpenCommand : public ItemListCommand<VGMFile> {
public:
  void executeItems(std::vector<VGMFile*> vgmfiles) const override {
    for (auto vgmfile : vgmfiles) {
      MdiArea::the()->newView(vgmfile);
    }
  }
  [[nodiscard]] QList<QKeySequence> shortcutKeySequences() const override { return {Qt::Key_Return}; };
  [[nodiscard]] std::string name() const override { return "Open Analysis"; }
  [[nodiscard]] std::optional<MenuPath> menuPath() const override { return MenuPaths::File; }
};
