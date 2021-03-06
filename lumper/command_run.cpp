//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include "lumper/commands.h"

#include <array>
#include <chrono>
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
#include "fmt/chrono.h"
#include "fmt/format.h"
#include "fmt/ranges.h"
#include "spdlog/spdlog.h"
#include "uuidxx/uuidxx.h"

#include "base/exception.h"
#include "base/ignore.h"
#include "base/subprocess.h"
#include "lumper/cgroups/cgroup_manager.h"
#include "lumper/container_info.h"
#include "lumper/mount_container_before_exec.h"
#include "lumper/path_constants.h"

namespace lumper {
namespace {

// Use last part of uuid-v4 as container-id.
inline std::string generate_container_id() {
    auto uuid = uuidxx::make_v4().to_string();
    return uuid.substr(uuid.rfind('-') + 1);
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

std::tuple<std::string, std::filesystem::path, std::string>
create_container_root(std::string_view image_name) {
    auto image_root = get_image_path(image_name);
    if (!std::filesystem::exists(image_root)) {
        // TODO(KC): untar image first if image_root doesn't exist.
        throw std::invalid_argument(
                fmt::format("image root ({}) doesn't exist", image_root.string()));
    }

    std::string container_id;
    while (true) {
        container_id = generate_container_id();
        auto created = std::filesystem::create_directory(get_container_path(container_id, ""));
        if (created) {
            SPDLOG_INFO("Successfully chosed container-id={}", container_id);
            break;
        }
        SPDLOG_WARN("Generated container-id({}) already in use, try another one", container_id);
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

    return {container_id, rootfs, mount_data};
}

inline std::string time_point_to_str(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    return fmt::format("{:%Y-%m-%d %H:%M:%S}", fmt::localtime(time));
}

esl::unique_fd create_file(const std::string& path) {
    constexpr int perm = 0666;
    int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, perm);
    if (fd == -1) {
        std::string what("create file failure: ");
        what += path;
        throw std::system_error(errno, std::system_category(), what);
    }

    return esl::wrap_unique_fd(fd);
}

} // namespace

void process(cli::cmd_run_t) {
    const auto& parser = cli::for_current_process().command_parser();

    auto image_name = parser.get<std::string>("--image");
    auto&& [container_id, container_root, root_mount_data] = create_container_root(image_name);

    base::subprocess::options opts;
    opts.clone_with_flags(CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET | CLONE_NEWIPC);

    // Since --detach and --it cannot be enabled both, and when they both are not enabled,
    // we assume --it ought be enabled.
    auto detach_mode = parser.get<bool>("--detach");
    SPDLOG_INFO("running in detach-mode={}", detach_mode);
    esl::unique_fd logfile_fd;
    if (detach_mode) {
        auto logfile = get_container_path(container_id, k_container_log_filename);
        logfile_fd = create_file(logfile.native());
        opts.set_stdout(base::subprocess::use_fd, logfile_fd.get());
        opts.set_stderr(base::subprocess::use_fd, logfile_fd.get());
        opts.detach();
    }

    mount_container_before_exec mount_container(container_id,
                                                container_root,
                                                std::move(root_mount_data));

    auto vol = parser.present("--volume");
    if (vol.has_value()) {
        auto parts = esl::strings::split(*vol, ':', esl::strings::skip_empty{})
                             .to<std::vector<std::string_view>>();
        assert(parts.size() == 2);
        if (!std::filesystem::exists(parts[0])) {
            throw command_run_error(
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
        container_info info;
        ESL_ON_SCOPE_EXIT {
            if (!detach_mode) {
                try {
                    base::ignore_unused(proc.wait());
                    info.status = k_container_status_stopped;
                    save_container_info(info);
                } catch (const std::exception& ex) {
                    // NOLINTNEXTLINE(bugprone-lambda-function-name)
                    SPDLOG_ERROR("Unexpected failure during waiting container to exit; "
                                 "ex={} container_id={}",
                                 ex.what(), info.id);
                }
            }
            // NOLINTNEXTLINE(bugprone-lambda-function-name)
            SPDLOG_INFO("Command {} completed", esl::strings::join(argv, " "));
        };

        cgroup_mgr.apply(proc.pid());
        info = container_info{container_id,
                              image_name,
                              esl::strings::join(argv, " "),
                              time_point_to_str(std::chrono::system_clock::now()),
                              k_container_status_running,
                              proc.pid()};
        save_container_info(info);
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
