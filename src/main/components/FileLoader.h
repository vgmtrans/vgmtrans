#pragma once

#include "RawFile.h"

#include <deque>
#include <memory>
#include <vector>

class FileLoader {
   public:
    virtual ~FileLoader() = default;
    virtual void apply(const RawFile *) = 0;
    [[nodiscard]] std::vector<std::unique_ptr<RawFile>> results();

   protected:
    void enqueue(RawFile* file);
    void enqueue(std::unique_ptr<RawFile> file);

   private:
    std::deque<std::unique_ptr<RawFile>> m_res;
};
