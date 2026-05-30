#include "FileLoader.h"

#include <algorithm>
#include <ranges>

std::vector<std::unique_ptr<RawFile>> FileLoader::results() {
  std::vector<std::unique_ptr<RawFile>> res;

  if (!m_res.empty()) {
    res.reserve(m_res.size());
    std::ranges::move(m_res, std::back_inserter(res));
    m_res.clear();
  }

  return res;
}

void FileLoader::enqueue(RawFile* file) {
  enqueue(std::unique_ptr<RawFile>(file));
}

void FileLoader::enqueue(std::unique_ptr<RawFile> file) {
  if (file) {
    m_res.emplace_back(std::move(file));
  }
}
