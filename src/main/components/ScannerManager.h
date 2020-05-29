#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>
#include <memory>
#include <type_traits>
#include <initializer_list>

class VGMScanner;
using scannerSpawner = std::function<std::shared_ptr<VGMScanner>()>;

class ScannerManager final {
   public:
    static ScannerManager &get() {
        static ScannerManager instance;
        return instance;
    }

    void add(const char *scanner_name, scannerSpawner gen) {
        m_generators.emplace(scanner_name, gen);
    }

    void add_extension_binding(const char *ext, scannerSpawner gen) {
        m_generators_ext[ext].push_back(gen);
    }

    std::vector<std::shared_ptr<VGMScanner>> scanners() const {
        std::vector<std::shared_ptr<VGMScanner>> tmp(m_generators.size());
        std::transform(m_generators.begin(), m_generators.end(), tmp.begin(),
                       [](auto pair) { return pair.second(); });

        return tmp;
    }

    std::vector<std::shared_ptr<VGMScanner>> scanners_with_extension(std::string ext) const {
        std::vector<std::shared_ptr<VGMScanner>> tmp;
        if (auto vec = m_generators_ext.find(ext); vec != m_generators_ext.end()) {
            tmp.resize(vec->second.size());
            std::transform(vec->second.begin(), vec->second.end(), tmp.begin(),
                           [](auto spawner) { return spawner(); });
        }

        return tmp;
    }

   private:
    ScannerManager() = default;
    ScannerManager(const ScannerManager &) = default;
    ScannerManager(ScannerManager &&) = default;
    ~ScannerManager() = default;

    std::unordered_map<std::string, scannerSpawner> m_generators;
    std::unordered_map<std::string, std::vector<scannerSpawner>> m_generators_ext;
};

namespace vgmcis::scanners {
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
}  // namespace vgmcis::scanners
