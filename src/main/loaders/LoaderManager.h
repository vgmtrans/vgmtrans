#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class FileLoader;
using loaderSpawner = std::function<std::shared_ptr<FileLoader>()>;

class LoaderManager final {
public:
  static LoaderManager& get() {
    static LoaderManager instance;
    return instance;
  }

  LoaderManager(const LoaderManager&) = delete;
  LoaderManager& operator=(const LoaderManager&) = delete;
  LoaderManager(LoaderManager&&) = delete;
  LoaderManager& operator=(LoaderManager&&) = delete;

  void add(const char* loader_name, loaderSpawner gen) {
    m_generators.try_emplace(loader_name, gen);
    m_loaders.clear();
  }

  const std::vector<std::shared_ptr<FileLoader>>& loaders() const {
    if (m_loaders.empty()) {
      m_loaders.resize(m_generators.size());
      std::transform(m_generators.begin(), m_generators.end(), m_loaders.begin(),
                     [](const auto& pair) { return pair.second(); });
    }

    return m_loaders;
  }

private:
  LoaderManager() = default;

  std::unordered_map<std::string, loaderSpawner> m_generators;
  mutable std::vector<std::shared_ptr<FileLoader>> m_loaders;
};

namespace vgmtrans::loaders {
template <typename T>
class LoaderRegistration final {
public:
  explicit LoaderRegistration(const char* id) { LoaderManager::get().add(id, std::make_shared<T>); }
};
}  // namespace vgmtrans::loaders
