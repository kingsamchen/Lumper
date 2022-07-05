//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include "lumper/commands.h"

#include <filesystem>
#include <string>
#include <vector>

#include "fmt/format.h"

#include "lumper/path_constants.h"

namespace lumper {

void process(cli::cmd_rm_t) {
    const auto& parser = cli::for_current_process().command_parser();

    auto ids = parser.get<std::vector<std::string>>("container_ids");
    for (const auto& id : ids) {
        auto container_path = std::filesystem::path(k_container_dir) / id;
        auto rm_cnt = std::filesystem::remove_all(container_path);
        if (rm_cnt > 0) {
            fmt::print("Container {} is deleted\n", id);
        } else {
            fmt::print("Contaienr {} not found\n", id);
        }
    }
}

} // namespace lumper
