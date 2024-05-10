#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>

class FileLoader;
using loaderSpawner = std::function<std::shared_ptr<FileLoader>()>;

class LoaderManager final {
 public:
  static LoaderManager &get() {
    static LoaderManager instance;
    return instance;
  }

  LoaderManager(const LoaderManager &) = delete;
  LoaderManager &operator=(const LoaderManager &) = delete;
  LoaderManager(LoaderManager &&) = delete;
  LoaderManager &operator=(LoaderManager &&) = delete;

  void add(const char *loader_name, loaderSpawner gen) {
    m_generators.try_emplace(loader_name, gen);
  }

  std::vector<std::shared_ptr<FileLoader>> loaders() const {
    std::vector<std::shared_ptr<FileLoader>> tmp(m_generators.size());
    std::transform(m_generators.begin(), m_generators.end(), tmp.begin(),
                   [](auto pair) { return pair.second(); });

    return tmp;
  }

 private:
  LoaderManager() = default;

  std::unordered_map<std::string, loaderSpawner> m_generators;
};

namespace vgmtrans::loaders {
template <typename T>
class LoaderRegistration final {
 public:
  explicit LoaderRegistration(const char *id) {
    LoaderManager::get().add(id, std::make_shared<T>);
  }
};
}  // namespace vgmtrans::loaders
