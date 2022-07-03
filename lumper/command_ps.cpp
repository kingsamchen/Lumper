//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include "lumper/commands.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "fmt/format.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "lumper/container_info.h"
#include "lumper/path_constants.h"

namespace lumper {
namespace {

constexpr char k_missing_value[] = "N/A";

void print_headline() {
    fmt::print("CONTAINER ID\t"
               "IMAGE\t"
               "COMMAND\t"
               "CREATED\t"
               "STATUS\t\n");
}

nlohmann::json load_container_info_json(std::string_view container_id) {
    auto json_path = std::filesystem::path(k_container_dir) / container_id / k_info_filename;
    std::ifstream in(json_path);
    try {
        return nlohmann::json::parse(in);
    } catch (const nlohmann::json::parse_error& ex) {
        SPDLOG_ERROR("Failed to parse container-info file into json; ex={} container_id={}",
                     ex.what(), container_id);
    }
    return nlohmann::json::object();
}

} // namespace

void process(cli::cmd_ps_t) {
    const auto& parser = cli::for_current_process().command_parser();

    bool list_all = parser.get<bool>("--all");

    print_headline();
    for (const auto& dir_entry : std::filesystem::directory_iterator{k_container_dir}) {
        auto container_id = dir_entry.path().filename().native();
        auto info_json = load_container_info_json(container_id);
        auto status = info_json.value("status", k_missing_value);
        if (!list_all && status != k_container_status_running) {
            continue;
        }
        auto image = info_json.value("image", k_missing_value);
        auto command = info_json.value("command", k_missing_value);
        auto created_time = info_json.value("create_time", k_missing_value);
        fmt::print("{}\t{}\t{}\t{}\t{}\t\n", container_id, image, command, created_time, status);
    }
}

} // namespace lumper
