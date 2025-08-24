/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "Format.h"
#include "services/MenuManager.h"
#include "VGMSeq.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMExport.h"
#include "VGMColl.h"
#include "formats/CPS/CPS1Instr.h"

namespace fs = std::filesystem;

/**
 * A command context for saving files. Provides a list of items to save and a path to save them to.
 */
template <typename TSavable>
class SaveCommandContext : public CommandContext {
public:
  SaveCommandContext() = default;

  void setItems(std::shared_ptr<std::vector<TSavable*>> f) {
    m_items = f;
  }
  void setSavePath(const std::string& p) {
    m_path = p;
  }

  [[nodiscard]] const std::vector<TSavable*>& items() const { return *m_items; }
  [[nodiscard]] const std::string& path() const { return m_path; }

private:
  std::shared_ptr<std::vector<TSavable*>> m_items{};
  std::string m_path;
};

/**
 * A command context factory for saving files. Will construct a context that provides a list of items and a path
 * to save them to.
 * @param alwaysSaveToDir Indicates that the command will expect a directory path always. Otherwise, a file path should
 * be provided when files contains a single item, and a directory path if files is multiple items.
 */
template <typename TSavable>
class SaveCommandContextFactory : public CommandContextFactory {
public:
  explicit SaveCommandContextFactory(bool alwaysSaveToDir, std::string fileExtension = "") :
        CommandContextFactory(),
        alwaysSaveToDir(alwaysSaveToDir),
        fileExtension(std::move(fileExtension)) {}

  void setFileExtension(const std::string& extension) {
    fileExtension = extension;
  }

  [[nodiscard]] std::shared_ptr<CommandContext> createContext(const PropertyMap& properties) const override {
    auto context = std::make_shared<SaveCommandContext<TSavable>>();

    if (!properties.contains("files") || !properties.contains("filePath")) {
      return nullptr;
    }

    auto files = get<std::shared_ptr<std::vector<TSavable*>>>(properties.at("files"));
    context->setItems(files);
    context->setSavePath(get<std::string>(properties.at("filePath")));
    return context;
  }

  [[nodiscard]] PropertySpecifications propertySpecifications() const override {
    auto filePathValueType = (alwaysSaveToDir) ? PropertySpecValueType::DirPath : PropertySpecValueType::Path;
    return {
        {"filePath", filePathValueType, "Path to the file or directory", fileExtension},
        {"files", PropertySpecValueType::ItemList, "Vector of files", static_cast<std::shared_ptr<std::vector<TSavable*>>>(nullptr)},
    };
  }

private:
  bool alwaysSaveToDir;
  std::string fileExtension;
};


/**
 * The base Command class for a Save command. The context provides a list of items to save and a path
 * to save them to.
 * @param alwaysSaveToDir Indicates that the command will expect a directory path always. Otherwise, a file path should
 * be expected when files contains a single item, and a directory path if files is multiple items.
 */
template <typename TSavable, typename TInContext = TSavable>
class SaveCommand : public Command {
public:
  explicit SaveCommand(bool alwaysSaveToDir = false)
      : m_contextFactory(std::make_shared<SaveCommandContextFactory<TInContext>>(alwaysSaveToDir)),
        alwaysSaveToDir(alwaysSaveToDir) {
  }

  void execute(CommandContext& context) override {
    auto& vgmContext = dynamic_cast<SaveCommandContext<TInContext>&>(context);
    const auto& files = vgmContext.items();
    const auto& path = vgmContext.path();

    if (alwaysSaveToDir) {
      // alwaysSaveToDir indicates we were given a directory path and Save() also expects a directory path
      for (auto file : files) {
        if (auto specificFile = dynamic_cast<TSavable*>(file)) {
          save(path, specificFile);
        }
      }
      return;
    }
    if (files.size() == 1) {
      // In this case, path is a file path
      if (auto file = dynamic_cast<TSavable*>(files[0])) {
        save(path, file);
        return;
      }
    }

    for (auto file : files) {
      // If alwaysSaveToDir is not set and there are multiple files, the path given is to a directory
      // and the Save() function still expects a file path, so we construct file paths for each file using GetName()
      if (auto specificFile = dynamic_cast<TSavable*>(file)) {
        auto fileExtension = (extension() == "") ? "" : (std::string(".") + extension());
        fs::path filePath = path / fs::path(file->name() + fileExtension);
        save(filePath.generic_string(), specificFile);
      }
    }
  }

  [[nodiscard]] std::shared_ptr<CommandContextFactory> contextFactory() const override {
    m_contextFactory->setFileExtension(extension());
    return m_contextFactory;
  }

  [[nodiscard]] virtual std::string extension() const = 0;
  virtual void save(const std::string& path, TSavable* specificFile) const = 0;

private:
  std::shared_ptr<SaveCommandContextFactory<TInContext>> m_contextFactory;
  bool alwaysSaveToDir;
};

template<typename TFile>
class SaveAsOriginalFormatCommand : public SaveCommand<TFile> {
public:
  SaveAsOriginalFormatCommand() : SaveCommand<TFile>(false) {}

  void save(const std::string& path, TFile* file) const override {
    conversion::saveAsOriginal(*file, path);
  }
  [[nodiscard]] std::string name() const override { return "Save as original format"; }
  [[nodiscard]] std::string extension() const override { return ""; }
};

class SaveAsMidiCommand : public SaveCommand<VGMSeq, VGMFile> {
public:
  SaveAsMidiCommand() : SaveCommand<VGMSeq, VGMFile>(false) {}

  void save(const std::string& path, VGMSeq* seq) const override {
    int numAssocColls = seq->assocColls.size();
    if (numAssocColls > 0) {
      if (numAssocColls > 1 && seq->format()->usesCollectionDataForSeqConversion()) {
        pRoot->UI_toast("This sequence format uses collection data as context for "
          "conversion, however, more than one collection is associated with the sequence. The first "
          "associated collection was used.\n\nYou can resolve this by selecting a conversion action "
          "on a collection directly.",
          ToastType::Info, 15000);
      }
      seq->saveAsMidi(path, seq->assocColls[0]);
    } else {
      if (seq->format()->usesCollectionDataForSeqConversion()) {
        pRoot->UI_toast("This sequence format uses collection data as context for "
          "conversion, however, there is currently no collection containing the sequence. Conversion "
          "results may improve if the sequence is first grouped into a collection with an "
          "associated instrument set.",
          ToastType::Info, 15000);
      }
      seq->saveAsMidi(path);
    }
  }
  [[nodiscard]] std::string name() const override { return "Save as MIDI"; }
  [[nodiscard]] std::string extension() const override { return "mid"; }
};


class SaveAsDLSCommand : public SaveCommand<VGMInstrSet, VGMFile> {
public:
  SaveAsDLSCommand() : SaveCommand<VGMInstrSet, VGMFile>(false) {}

  void save(const std::string& path, VGMInstrSet* instrSet) const override {
    conversion::saveAsDLS(*instrSet, path);
  }
  [[nodiscard]] std::string name() const override { return "Save as DLS"; }
  [[nodiscard]] std::string extension() const override { return "dls"; }
};


class SaveAsSF2Command : public SaveCommand<VGMInstrSet, VGMFile> {
public:
  SaveAsSF2Command() : SaveCommand<VGMInstrSet, VGMFile>(false) {}

  void save(const std::string& path, VGMInstrSet* instrSet) const override {
    conversion::saveAsSF2(*instrSet, path);
  }
  [[nodiscard]] std::string name() const override { return "Save as SF2"; }
  [[nodiscard]] std::string extension() const override { return "sf2"; }
};

class SaveAsOPMCommand : public SaveCommand<CPS1OPMInstrSet, VGMFile> {
public:
  SaveAsOPMCommand() : SaveCommand<CPS1OPMInstrSet, VGMFile>(false) {}

  void save(const std::string& path, CPS1OPMInstrSet* instrSet) const override {
    instrSet->saveAsOPMFile(path);
  }
  [[nodiscard]] std::string name() const override { return "Save as OPM"; }
  [[nodiscard]] std::string extension() const override { return "opm"; }
};


class SaveWavBatchCommand : public SaveCommand<VGMSampColl, VGMFile> {
public:
  SaveWavBatchCommand() : SaveCommand<VGMSampColl, VGMFile>(true) {}

  void save(const std::string& path, VGMSampColl* sampColl) const override {
    conversion::saveAllAsWav(*sampColl, path);
  }
  [[nodiscard]] std::string name() const override { return "Save all samples as WAV"; }
  [[nodiscard]] std::string extension() const override { return "wav"; }
};

template <conversion::Target options>
class SaveCollCommand : public SaveCommand<VGMColl> {
public:
  SaveCollCommand() : SaveCommand<VGMColl>(true) {}

  void save(const std::string& path, VGMColl* coll) const override {
    conversion::saveAs<options>(*coll, path);
  }
  [[nodiscard]] std::string name() const override {
    std::vector<std::string> parts;
    if constexpr ((options & conversion::Target::MIDI) != 0) {
      parts.emplace_back("MIDI");
    }
    if constexpr ((options & conversion::Target::SF2) != 0) {
      parts.emplace_back("SF2");
    }
    if constexpr ((options & conversion::Target::DLS) != 0) {
      parts.emplace_back("DLS");
    }

    std::string name = "Export as ";
    for (size_t i = 0; i < parts.size(); ++i) {
      name += parts[i];
      if (i < parts.size() - 1) {
        if (parts.size() == 2) {            // Only two options: use " and " between them
          name += " and ";
        } else if (i < parts.size() - 2) {  // More than two options, comma separator
          name += ", ";
        } else {                            // Last element in a list of more than two, use ", and "
          name += ", and ";
        }
      }
    }
    return name;
  }
  [[nodiscard]] std::string extension() const override { return ""; }
};