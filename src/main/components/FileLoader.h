#pragma once

#include <deque>
#include <vector>
#include <memory>

#include "RawFile.h"

class FileLoader {
   public:
    virtual ~FileLoader() = default;
    virtual void apply(const RawFile *) = 0;
    [[nodiscard]] std::vector<RawFile*> results();

   protected:
    void enqueue(RawFile* file);

   private:
    std::deque<RawFile*> m_res;
};
