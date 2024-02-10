/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include <ghc/filesystem.hpp>

#include "services/MenuManager.h"
#include "VGMSeq.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "CPS1Instr.h"

namespace fs = ghc::filesystem;

/**
 * A command context for saving files. Provides a list of items to save and a path to save them to.
 */
template <typename TSavable>
class SaveCommandContext : public CommandContext {
private:
  shared_ptr<vector<TSavable*>> items{};
  string path;

public:
  SaveCommandContext() = default;

  void SetItems(shared_ptr<vector<TSavable*>> f) {
    items = f;
  }
  void SetSavePath(const string& p) {
    path = p;
  }

  [[nodiscard]] const vector<TSavable*>& GetItems() const { return *items; }
  [[nodiscard]] const string& GetPath() const { return path; }
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

  [[nodiscard]] shared_ptr<CommandContext> CreateContext(const PropertyMap& properties) const override {
    auto context = make_shared<SaveCommandContext<TSavable>>();

    if (properties.find("files") == properties.end() || properties.find("filePath") == properties.end()) {
      return nullptr;
    }

    auto files = get<shared_ptr<vector<TSavable*>>>(properties.at("files"));
    context->SetItems(files);
    context->SetSavePath(get<string>(properties.at("filePath")));
    return context;
  }

  [[nodiscard]] PropertySpecifications GetPropertySpecifications() const override {
    auto filePathValueType = (alwaysSaveToDir) ? PropertySpecValueType::DirPath : PropertySpecValueType::Path;
    return {
        {"filePath", filePathValueType, "Path to the file or directory", fileExtension},
        {"files", PropertySpecValueType::ItemList, "Vector of files", static_cast<shared_ptr<vector<TSavable*>>>(nullptr)},
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
  shared_ptr<SaveCommandContextFactory<TInContext>> contextFactory;
  bool alwaysSaveToDir;

public:
  explicit SaveCommand(bool alwaysSaveToDir = false)
      : contextFactory(make_shared<SaveCommandContextFactory<TInContext>>(alwaysSaveToDir)),
        alwaysSaveToDir(alwaysSaveToDir) {
  }

  void Execute(CommandContext& context) override {
    auto& vgmContext = dynamic_cast<SaveCommandContext<TInContext>&>(context);
    const auto& files = vgmContext.GetItems();
    const auto& path = vgmContext.GetPath();

    if (alwaysSaveToDir) {
      // alwaysSaveToDir indicates we were given a directory path and Save() also expects a directory path
      for (auto file : files) {
        auto specificFile = dynamic_cast<TSavable*>(file);
        if (specificFile) {
          Save(path, specificFile);
        }
        return;
      }
    }
    if (files.size() == 1) {
      // In this case, path is a file path
      auto file = dynamic_cast<TSavable*>(files[0]);
      if (file) {
        Save(path, file);
        return;
      }
    }

    for (auto file : files) {
      // If alwaysSaveToDir is not set and there are multiple files, the path given is to a directory
      // and the Save() function still expects a file path, so we construct file paths for each file using GetName()
      auto specificFile = dynamic_cast<TSavable*>(file);
      if (specificFile) {
        auto fileExtension = (GetExtension() == "") ? "" : (string(".") + GetExtension());
        fs::path filePath = path / fs::path(*file->GetName() + fileExtension);
        Save(filePath.generic_string(), specificFile);
      }
    }
  }

  [[nodiscard]] shared_ptr<CommandContextFactory> GetContextFactory() const override {
    contextFactory->SetFileExtension(GetExtension());
    return contextFactory;
  }

  [[nodiscard]] virtual std::string GetExtension() const = 0;
  virtual void Save(const string& path, TSavable* specificFile) const = 0;
};

class SaveAsOriginalFormatCommand : public SaveCommand<VGMFile> {
public:
  SaveAsOriginalFormatCommand() : SaveCommand<VGMFile>(false) {}

  void Save(const string& path, VGMFile* file) const override {
    file->OnSaveAsRaw(path);
  }
  [[nodiscard]] string Name() const override { return "Save as original format"; }
  [[nodiscard]] string GetExtension() const override { return ""; }
};


class SaveAsMidiCommand : public SaveCommand<VGMSeq, VGMFile> {
public:
  SaveAsMidiCommand() : SaveCommand<VGMSeq, VGMFile>(false) {}

  void Save(const string& path, VGMSeq* seq) const override {
    seq->SaveAsMidi(path);
  }
  [[nodiscard]] string Name() const override { return "Save as MIDI"; }
  [[nodiscard]] string GetExtension() const override { return "mid"; }
};


class SaveAsDLSCommand : public SaveCommand<VGMInstrSet, VGMFile> {
public:
  SaveAsDLSCommand() : SaveCommand<VGMInstrSet, VGMFile>(false) {}

  void Save(const string& path, VGMInstrSet* instrSet) const override {
    instrSet->SaveAsDLS(path);
  }
  [[nodiscard]] string Name() const override { return "Save as DLS"; }
  [[nodiscard]] string GetExtension() const override { return "dls"; }
};


class SaveAsSF2Command : public SaveCommand<VGMInstrSet, VGMFile> {
public:
  SaveAsSF2Command() : SaveCommand<VGMInstrSet, VGMFile>(false) {}

  void Save(const string& path, VGMInstrSet* instrSet) const override {
    instrSet->SaveAsSF2(path);
  }
  [[nodiscard]] string Name() const override { return "Save as SF2"; }
  [[nodiscard]] string GetExtension() const override { return "sf2"; }
};

class SaveAsOPMCommand : public SaveCommand<CPS1OPMInstrSet, VGMFile> {
public:
  SaveAsOPMCommand() : SaveCommand<CPS1OPMInstrSet, VGMFile>(false) {}

  void Save(const string& path, CPS1OPMInstrSet* instrSet) const override {
    instrSet->SaveAsOPMFile(path);
  }
  [[nodiscard]] string Name() const override { return "Save as OPM"; }
  [[nodiscard]] string GetExtension() const override { return "opm"; }
};


class SaveWavBatchCommand : public SaveCommand<VGMSampColl, VGMFile> {
public:
  SaveWavBatchCommand() : SaveCommand<VGMSampColl, VGMFile>(true) {}

  void Save(const string& path, VGMSampColl* sampColl) const override {
    sampColl->SaveAllAsWav(path);
  }
  [[nodiscard]] string Name() const override { return "Save all samples as WAV"; }
  [[nodiscard]] string GetExtension() const override { return "wav"; }
};
