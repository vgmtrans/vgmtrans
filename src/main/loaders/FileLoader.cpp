#include "FileLoader.h"

#include <algorithm>

std::vector<std::shared_ptr<RawFile>> FileLoader::results() {
    std::vector<std::shared_ptr<RawFile>> res;

    std::move(std::begin(m_res), std::end(m_res), std::back_inserter(res));
    m_res.clear();

    return res;
}

void FileLoader::enqueue(std::shared_ptr<RawFile> file) {
    m_res.emplace_back(file);
}
