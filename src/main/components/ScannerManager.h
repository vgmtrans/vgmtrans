#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>
#include <memory>
#include <initializer_list>

class VGMScanner;
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

  void add(const char *scanner_name, scannerSpawner gen) {
    m_generators.emplace(scanner_name, std::move(gen));
  }

  void add_extension_binding(const char *ext, scannerSpawner gen) {
    m_generators_ext[ext].emplace_back(std::move(gen));
  }

  std::vector<std::shared_ptr<VGMScanner>> scanners() const {
    std::vector<std::shared_ptr<VGMScanner>> tmp(m_generators.size());
    std::ranges::transform(m_generators, tmp.begin(), [](auto pair) { return pair.second(); });

    return tmp;
  }

  std::vector<std::shared_ptr<VGMScanner>> scanners_with_extension(const std::string& ext) const {
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
    explicit ScannerRegistration(const char *id) {
        ScannerManager::get().add(id, std::make_shared<T>);
    }

    ScannerRegistration(const char *id, std::initializer_list<const char *> exts) {
        auto &sm = ScannerManager::get();
        sm.add(id, std::make_shared<T>);
        for(auto ext : exts) {
            sm.add_extension_binding(ext, std::make_shared<T>);
        }
    }
};
}  // namespace vgmtrans::scanners
