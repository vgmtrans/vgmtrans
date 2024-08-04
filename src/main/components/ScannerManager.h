#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>
#include <memory>
#include <initializer_list>
#include "Format.h"
#include "Scanner.h"

using scannerSpawner = std::function<std::shared_ptr<VGMScanner>()>;

class ScannerManager final {
 public:
  static ScannerManager &get() {
    static ScannerManager instance;
    return instance;
  }

  ScannerManager(const ScannerManager &) = delete;
  ScannerManager &operator=(const ScannerManager &) = delete;
  ScannerManager(ScannerManager &&) = delete;
  ScannerManager &operator=(ScannerManager &&) = delete;

  void add(const char *format_name, scannerSpawner gen) {
    m_generators.emplace(format_name, std::move(gen));
  }

  void addExtensionBinding(const char *ext, scannerSpawner gen) {
    m_generators_ext[ext].emplace_back(std::move(gen));
  }

  std::vector<std::shared_ptr<VGMScanner>> scanners() const {
    std::vector<std::shared_ptr<VGMScanner>> tmp(m_generators.size());
    std::ranges::transform(m_generators, tmp.begin(), [](auto pair) { return pair.second(); });

    return tmp;
  }

  std::vector<std::shared_ptr<VGMScanner>> scannersWithExtension(const std::string& ext) const {
    std::vector<std::shared_ptr<VGMScanner>> tmp;
    if (auto vec = m_generators_ext.find(ext); vec != m_generators_ext.end()) {
      tmp.resize(vec->second.size());
      std::ranges::transform(vec->second, tmp.begin(), [](auto spawner) { return spawner(); });
    }

    return tmp;
  }

 private:
  ScannerManager() = default;

  std::unordered_map<std::string, scannerSpawner> m_generators;
  std::unordered_map<std::string, std::vector<scannerSpawner>> m_generators_ext;
};

namespace vgmtrans::scanners {
template <typename T>
class ScannerRegistration final {
 public:
  explicit ScannerRegistration(const char* formatName) {
    ScannerManager::get().add(formatName, [formatName]() -> std::shared_ptr<VGMScanner> {
      auto format = Format::formatFromName(formatName);
      return std::make_shared<T>(format);
    });
  }

  ScannerRegistration(const char* formatName, std::initializer_list<const char*> exts) {
    auto &sm = ScannerManager::get();
    sm.add(formatName, [formatName]() -> std::shared_ptr<VGMScanner> {
      auto format = Format::formatFromName(formatName);
      return std::make_shared<T>(format);
    });

    for (auto ext : exts) {
      sm.addExtensionBinding(ext, [formatName]() -> std::shared_ptr<VGMScanner> {
        auto format = Format::formatFromName(formatName);
        return std::make_shared<T>(format);
      });
    }
  }
};
}  // namespace vgmtrans::scanners
