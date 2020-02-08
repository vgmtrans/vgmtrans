/*
 * VGMTransQt (c) 2020
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <string>
#include <fstream>
#include "json.hpp"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"

namespace conversion {
using json = nlohmann::json;

void DumpToJSON(std::string path, VGMSeq &seq) {
    json out;

    out["name"] = seq.name();
    out["format"] = seq.GetFormatName();

    auto track_list = json::array();
    for (auto track : seq.aTracks) {
        auto evt_list = json::array();
        for (auto evt : track->aEvents) {
            auto jevt = json::object();

            jevt.push_back({"type", evt->name});
            jevt.push_back({"description", evt->GetDescription()});
            jevt.push_back({"offset", evt->dwOffset});
            jevt.push_back({"size", evt->unLength});
            evt_list.push_back(jevt);
        }

        track_list.push_back(evt_list);
    }

    out["tracks"] = track_list;
    std::ofstream o(path);
    o << out.dump(4, ' ', false, json::error_handler_t::replace) << std::endl;
}
}  // namespace conversion
