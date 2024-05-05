#include "FileLoader.h"

#include <algorithm>

std::vector<RawFile*> FileLoader::results() {
    std::vector<RawFile*> res;

    if (!m_res.empty()) {
        std::move(std::begin(m_res), std::end(m_res), std::back_inserter(res));
        m_res.clear();
    }

    return res;
}

void FileLoader::enqueue(RawFile* file) {
    m_res.emplace_back(file);
}
