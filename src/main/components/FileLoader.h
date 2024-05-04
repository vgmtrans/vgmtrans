#pragma once

#include <deque>
#include <vector>
#include <memory>

#include "RawFile.h"

class FileLoader {
   public:
    virtual ~FileLoader() = default;
    virtual void apply(const RawFile *) = 0;
    [[nodiscard]] std::vector<std::shared_ptr<RawFile>> results();

   protected:
    void enqueue(std::shared_ptr<RawFile> file);

   private:
    std::deque<std::shared_ptr<RawFile>> m_res;
};
