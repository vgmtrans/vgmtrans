/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "services/MenuManager.h"
#include "VGMSeq.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMExport.h"

namespace fs = std::filesystem;

/**
 * A command context for saving files. Provides a list of items to save and a path to save them to.
 */
template <typename TSavable>
class SaveCommandContext : public CommandContext {
private:
  std::shared_ptr<std::vector<TSavable*>> items{};
  std::string path;

public:
  SaveCommandContext() = default;

  void SetItems(std::shared_ptr<std::vector<TSavable*>> f) {
    items = f;
  }
  void SetSavePath(const std::string& p) {
    path = p;
  }

  [[nodiscard]] const std::vector<TSavable*>& GetItems() const { return *items; }
  [[nodiscard]] const std::string& GetPath() const { return path; }
};

/**
 * A command context factory for saving files. Will construct a context that provides a list of items and a path
 * to save them to.
 * @param alwaysSaveToDir Indicates that the command will expect a directory path always. Otherwise, a file path should
 * be provided when files contains a single item, and a directory path if files is multiple items.
 */
template <typename TSavable>
class SaveCommandContextFactory : public CommandContextFactory {
private:
  bool alwaysSaveToDir;
  std::string fileExtension;

public:
  explicit SaveCommandContextFactory(bool alwaysSaveToDir, std::string fileExtension = "") :
        CommandContextFactory(),
        alwaysSaveToDir(alwaysSaveToDir),
        fileExtension(std::move(fileExtension)) {}

  void SetFileExtension(const std::string& extension) {
    fileExtension = extension;
  }

  [[nodiscard]] std::shared_ptr<CommandContext> CreateContext(const PropertyMap& properties) const override {
    auto context = std::make_shared<SaveCommandContext<TSavable>>();

    if (!properties.contains("files") || !properties.contains("filePath")) {
      return nullptr;
    }

    auto files = get<std::shared_ptr<std::vector<TSavable*>>>(properties.at("files"));
    context->SetItems(files);
    context->SetSavePath(get<std::string>(properties.at("filePath")));
    return context;
  }

  [[nodiscard]] PropertySpecifications GetPropertySpecifications() const override {
    auto filePathValueType = (alwaysSaveToDir) ? PropertySpecValueType::DirPath : PropertySpecValueType::Path;
    return {
        {"filePath", filePathValueType, "Path to the file or directory", fileExtension},
        {"files", PropertySpecValueType::ItemList, "Vector of files", static_cast<std::shared_ptr<std::vector<TSavable*>>>(nullptr)},
    };
  }
};


/**
 * The base Command class for a Save command. The context provides a list of items to save and a path
 * to save them to.
 * @param alwaysSaveToDir Indicates that the command will expect a directory path always. Otherwise, a file path should
 * be expected when files contains a single item, and a directory path if files is multiple items.
 */
template <typename TSavable, typename TInContext = TSavable>
class SaveCommand : public Command {
private:
  std::shared_ptr<SaveCommandContextFactory<TInContext>> contextFactory;
  bool alwaysSaveToDir;

public:
  explicit SaveCommand(bool alwaysSaveToDir = false)
      : contextFactory(std::make_shared<SaveCommandContextFactory<TInContext>>(alwaysSaveToDir)),
        alwaysSaveToDir(alwaysSaveToDir) {
  }

  void Execute(CommandContext& context) override {
    auto& vgmContext = dynamic_cast<SaveCommandContext<TInContext>&>(context);
    const auto& files = vgmContext.GetItems();
    const auto& path = vgmContext.GetPath();

    if (alwaysSaveToDir) {
      // alwaysSaveToDir indicates we were given a directory path and Save() also expects a directory path
      for (auto file : files) {
        if (auto specificFile = dynamic_cast<TSavable*>(file)) {
          Save(path, specificFile);
        }
        return;
      }
    }
    if (files.size() == 1) {
      // In this case, path is a file path
      if (auto file = dynamic_cast<TSavable*>(files[0])) {
        Save(path, file);
        return;
      }
    }

    for (auto file : files) {
      // If alwaysSaveToDir is not set and there are multiple files, the path given is to a directory
      // and the Save() function still expects a file path, so we construct file paths for each file using GetName()
      if (auto specificFile = dynamic_cast<TSavable*>(file)) {
        auto fileExtension = (GetExtension() == "") ? "" : (std::string(".") + GetExtension());
        fs::path filePath = path / fs::path(file->name() + fileExtension);
        Save(filePath.generic_string(), specificFile);
      }
    }
  }

  [[nodiscard]] std::shared_ptr<CommandContextFactory> GetContextFactory() const override {
    contextFactory->SetFileExtension(GetExtension());
    return contextFactory;
  }

  [[nodiscard]] virtual std::string GetExtension() const = 0;
  virtual void Save(const std::string& path, TSavable* specificFile) const = 0;
};

template<typename TFile>
class SaveAsOriginalFormatCommand : public SaveCommand<TFile> {
public:
  SaveAsOriginalFormatCommand() : SaveCommand<TFile>(false) {}

  void Save(const std::string& path, TFile* file) const override {
    conversion::SaveAsOriginal(*file, path);
  }
  [[nodiscard]] std::string Name() const override { return "Save as original format"; }
  [[nodiscard]] std::string GetExtension() const override { return ""; }
};

class SaveAsMidiCommand : public SaveCommand<VGMSeq, VGMFile> {
public:
  SaveAsMidiCommand() : SaveCommand<VGMSeq, VGMFile>(false) {}

  void Save(const std::string& path, VGMSeq* seq) const override {
    seq->SaveAsMidi(path);
  }
  [[nodiscard]] std::string Name() const override { return "Save as MIDI"; }
  [[nodiscard]] std::string GetExtension() const override { return "mid"; }
};


class SaveAsDLSCommand : public SaveCommand<VGMInstrSet, VGMFile> {
public:
  SaveAsDLSCommand() : SaveCommand<VGMInstrSet, VGMFile>(false) {}

  void Save(const std::string& path, VGMInstrSet* instrSet) const override {
    conversion::SaveAsDLS(*instrSet, path);
  }
  [[nodiscard]] std::string Name() const override { return "Save as DLS"; }
  [[nodiscard]] std::string GetExtension() const override { return "dls"; }
};


class SaveAsSF2Command : public SaveCommand<VGMInstrSet, VGMFile> {
public:
  SaveAsSF2Command() : SaveCommand<VGMInstrSet, VGMFile>(false) {}

  void Save(const std::string& path, VGMInstrSet* instrSet) const override {
    conversion::SaveAsSF2(*instrSet, path);
  }
  [[nodiscard]] std::string Name() const override { return "Save as SF2"; }
  [[nodiscard]] std::string GetExtension() const override { return "sf2"; }
};


class SaveWavBatchCommand : public SaveCommand<VGMSampColl, VGMFile> {
public:
  SaveWavBatchCommand() : SaveCommand<VGMSampColl, VGMFile>(true) {}

  void Save(const std::string& path, VGMSampColl* sampColl) const override {
    conversion::SaveAllAsWav(*sampColl, path);
  }
  [[nodiscard]] std::string Name() const override { return "Save all samples as WAV"; }
  [[nodiscard]] std::string GetExtension() const override { return "wav"; }
};
