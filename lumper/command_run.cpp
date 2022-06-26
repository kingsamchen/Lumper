//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include "lumper/commands.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <tuple>
#include <utility>

#include <sched.h>
#include <unistd.h>

#include "esl/scope_guard.h"
#include "esl/strings.h"
#include "fmt/format.h"
#include "fmt/ranges.h"
#include "spdlog/spdlog.h"
#include "uuidxx/uuidxx.h"

#include "base/exception.h"
#include "base/ignore.h"
#include "base/subprocess.h"
#include "lumper/cgroups/cgroup_manager.h"
#include "lumper/mount_container_before_exec.h"

namespace lumper {
namespace {

constexpr char k_images_dir[] = "/var/lib/lumper/images";
constexpr char k_container_dir[] = "/var/lib/lumper/containers";

// Use last part of uuid-v4 as hostname.
std::string hostname_from_contaienr_id(std::string_view id) {
    auto pos = id.rfind('-');
    if (pos == std::string_view::npos) {
        throw std::invalid_argument(fmt::format("invalid container-id({})", id));
    }

    return std::string(id.substr(pos + 1));
}

std::filesystem::path get_image_path(std::string_view image_name) {
    std::filesystem::path path(k_images_dir);
    path /= image_name;
    return path;
}

std::filesystem::path get_container_path(std::string_view container_id, std::string_view subdir) {
    std::filesystem::path path(k_container_dir);
    path /= container_id;
    path /= subdir;
    return path;
}

std::tuple<std::filesystem::path, std::string>
create_container_root(std::string_view image_name, std::string_view container_id) {
    auto image_root = get_image_path(image_name);
    if (!std::filesystem::exists(image_root)) {
        // TODO(KC): untar image first if image_root doesn't exist.
        throw std::invalid_argument(
                fmt::format("image root ({}) doesn't exist", image_root.string()));
    }

    // Create directories for:
    //  - cow layer (upperdir)
    //  - overlay workdir
    //  - a mount point
    auto cow_rw = get_container_path(container_id, "cow_rw");
    auto cow_workdir = get_container_path(container_id, "cow_workdir");
    auto rootfs = get_container_path(container_id, "rootfs");
    for (const auto& path : {std::cref(cow_rw), std::cref(cow_workdir), std::cref(rootfs)}) {
        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
        }
    }

    auto mount_data = fmt::format("lowerdir={},upperdir={},workdir={}",
                                  image_root.native(), cow_rw.native(), cow_workdir.native());

    SPDLOG_INFO("Create container root; image_root={}\ncontainer_root={}\nmount_data={}",
                image_root.native(), rootfs.native(), mount_data);

    return {rootfs, mount_data};
}

} // namespace

void process(cli::cmd_run_t) {
    const auto& parser = cli::for_current_process().command_parser();

    base::subprocess::options opts;
    opts.clone_with_flags(CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET | CLONE_NEWIPC);

    // Since --detach and --it cannot be enabled both, and when they both are not enabled,
    // we assume --it ought be enabled.
    auto detach_mode = parser.get<bool>("--detach");
    SPDLOG_INFO("running in detach-mode={}", detach_mode);
    if (detach_mode) {
        opts.set_stdin(base::subprocess::use_null);
        opts.set_stdout(base::subprocess::use_null);
        opts.set_stderr(base::subprocess::use_null);
        opts.detach();
    }

    auto container_id = uuidxx::make_v4().to_string();
    auto&& [container_root, root_mount_data] =
            create_container_root(parser.get<std::string>("--image"), container_id);

    mount_container_before_exec mount_container(hostname_from_contaienr_id(container_id),
                                                container_root,
                                                std::move(root_mount_data));

    auto vol = parser.present("--volume");
    if (vol.has_value()) {
        auto parts = esl::strings::split(*vol, ':', esl::strings::skip_empty{})
                             .to<std::vector<std::string_view>>();
        assert(parts.size() == 2);
        if (!std::filesystem::exists(parts[0])) {
            throw std::invalid_argument(
                    fmt::format("volume path ({}) in host doesn't exist", parts[0]));
        }

        auto container_vol = container_root / std::filesystem::path(parts[1]).relative_path();
        mount_container.set_volume_dir({std::string(parts[0]), container_vol.native()});
    }

    opts.set_evil_pre_exec_callback(&mount_container);

    cgroups::resource_config res_cfg;

    auto mem_limit = parser.present<std::string>("--memory");
    if (mem_limit) {
        res_cfg.set_memory_limit(*mem_limit);
    }

    auto cpus_limit = parser.present<int>("--cpus");
    if (cpus_limit) {
        res_cfg.set_cpus(*cpus_limit);
    }

    auto argv = parser.get<std::vector<std::string>>("CMD");
    SPDLOG_INFO("Prepare to run cmd: {}", argv);
    try {
        cgroups::cgroup_manager cgroup_mgr("lumper-cgroup", res_cfg);

        base::subprocess proc(argv, opts);
        ESL_ON_SCOPE_EXIT {
            if (!detach_mode) {
                base::ignore_unused(proc.wait());
            }
            // NOLINTNEXTLINE(bugprone-lambda-function-name)
            SPDLOG_INFO("Command {} completed", esl::strings::join(argv, " "));
        };

        cgroup_mgr.apply(proc.pid());
    } catch (const base::spawn_subprocess_error& ex) {
        auto errc = mount_container.read_error();
        if (errc != mount_errc::ok) {
            SPDLOG_ERROR("Failed to run mount_proc_before_exec; reason={}", mount_errc_msg(errc));
        }
        throw command_run_error(ex.what());
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Failed to run cmd in sub-process; cmd={}", argv);
        throw command_run_error(ex.what());
    }
}

} // namespace lumper
