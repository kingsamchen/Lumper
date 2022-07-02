//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include "lumper/container_info.h"

#include <filesystem>
#include <fstream>

#include "nlohmann/json.hpp"

#include "lumper/path_constants.h"

namespace lumper {

void to_json(nlohmann::json& j, const container_info& info) {
    j = nlohmann::json{
            {"id", info.id},
            {"image", info.image},
            {"command", info.command},
            {"create_time", info.create_time},
            {"status", info.status},
            {"pid", info.pid}};
}

void from_json(const nlohmann::json& j, container_info& info) {
    j.at("id").get_to(info.id);
    j.at("image").get_to(info.image);
    j.at("command").get_to(info.command);
    j.at("create_time").get_to(info.create_time);
    j.at("status").get_to(info.status);
    j.at("pid").get_to(info.pid);
}

void save_container_info(const container_info& info) {
    auto info_path = std::filesystem::path(k_container_dir) / info.id / k_info_filename;
    nlohmann::json config;
    to_json(config, info);
    std::ofstream out(info_path);
    out << config;
}

} // namespace lumper
