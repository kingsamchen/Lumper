//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#pragma once

#ifndef LUMPER_CONTAINER_INFO_H_
#define LUMPER_CONTAINER_INFO_H_

#include <string>

#include "nlohmann/json_fwd.hpp"

namespace lumper {

constexpr char k_container_status_stopped[] = "stopped";
constexpr char k_container_status_running[] = "running";

struct container_info {
    std::string id;
    std::string image;
    std::string command;
    std::string create_time;
    std::string status;
    int pid;
};

void to_json(nlohmann::json& j, const container_info& info);

void from_json(const nlohmann::json& j, container_info& info);

void save_container_info(const container_info& info);

} // namespace lumper

#endif // LUMPER_CONTAINER_INFO_H_
