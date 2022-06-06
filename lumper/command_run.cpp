//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include "lumper/commands.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <utility>

#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "esl/scope_guard.h"
#include "esl/strings.h"
#include "esl/unique_handle.h"
#include "fmt/ranges.h"
#include "spdlog/spdlog.h"

#include "base/exception.h"
#include "base/ignore.h"
#include "base/subprocess.h"
#include "lumper/cgroups/cgroup_manager.h"

namespace lumper {
namespace {

enum class mount_errc : std::uint32_t {
    ok = 0,
    mount_private,
    mount_proc,
    mount_sys,
    mount_dev,
    mount_as_bind,
    mount_container_root,
    mkdir_old_root_for_pivot,
    syscall_pivot_root,
    chdir_call,
    unmount_old_pivot,
    rmdir_old_pivot,
    total_count
};

const char* mount_errc_msg(mount_errc errc) noexcept {
    constexpr const char* errc_msgs[] = {"success",
                                         "failed to mount for private namespace",
                                         "failed to mount /proc as proc",
                                         "failed to mount /sys as sysfs",
                                         "failed to mount /dev as tmpfs",
                                         "failed to mount new root as bind",
                                         "failed to mount container root",
                                         "failed to mkdir old root for pivot",
                                         "failed to call syscall pivot_root",
                                         "failed to chdir to new root",
                                         "failed to unmount old root",
                                         "failed to rmdir old root"};
    static_assert(std::size(errc_msgs) == std::size_t(mount_errc::total_count));
    auto idx = static_cast<std::underlying_type_t<mount_errc>>(errc);
    return errc_msgs[idx];
}

template<std::size_t N>
constexpr auto old_root_after_pivot(const char (&pivot_root)[N]) -> std::array<char, N + 1> {
    std::array<char, N + 1> str{'/'};
    for (std::size_t i = 0; i < N - 1; ++i) {
        str[i + 1] = pivot_root[i];
    }
    return str;
}

class mount_proc_before_exec : public base::subprocess::evil_pre_exec_callback {
public:
    explicit mount_proc_before_exec(const std::filesystem::path& new_root)
        : new_root_(new_root),
          old_root_(new_root / k_old_root_name),
          new_proc_(new_root / "proc"),
          new_sys_(new_root / "sys"),
          new_dev_(new_root / "dev") {
        int fds[2]{};
        if (::pipe2(fds, O_CLOEXEC) != 0) {
            throw std::system_error(errno,
                                    std::system_category(),
                                    "failed to pipe2() for mount_proc_before_exec");
        }

        err_pipe_rd_ = esl::wrap_unique_fd(fds[0]);
        err_pipe_wr_ = esl::wrap_unique_fd(fds[1]);
    }

    // No dynamic allocation is allowed in this function.
    int run() noexcept override {
        auto errc = make_contained();
        if (errc != mount_errc::ok) {
            auto err_value = errno;
            ssize_t wc = 0;
            do {
                wc = ::write(err_pipe_wr_.get(), &errc, sizeof(errc));
            } while (wc == -1 && errno == EINTR);
            return err_value;
        }
        return 0;
    }

    mount_errc read_error() {
        err_pipe_wr_.reset();
        auto errc = mount_errc::ok;
        ssize_t rc = 0;
        do {
            rc = ::read(err_pipe_rd_.get(), &errc, sizeof(errc));
        } while (rc == -1 && errno == EINTR);
        return errc;
    }

private:
    mount_errc make_contained() noexcept {
        // See https://man7.org/linux/man-pages/man7/mount_namespaces.7.html#NOTES
        // `MS_REC` here to apply recursively.
        if (::mount("", "/", "", MS_PRIVATE | MS_REC, "") != 0) {
            return mount_errc::mount_private;
        }

        if (auto errc = setup_container_root(); errc != mount_errc::ok) {
            return errc;
        }

        if (auto errc = create_mounts(); errc != mount_errc::ok) {
            return errc;
        }

        if (auto errc = change_root(); errc != mount_errc::ok) {
            return errc;
        }

        return mount_errc::ok;
    }

    mount_errc setup_container_root() const noexcept {
        FORCE_AS_MEMBER_FUNCTION();
        return mount_errc::ok;
    }

    mount_errc create_mounts() const noexcept {
        if (::mount("proc", new_proc_.c_str(), "proc", 0, "") != 0) {
            return mount_errc::mount_proc;
        }

        if (::mount("sysfs", new_sys_.c_str(), "sysfs", 0, "") != 0) {
            return mount_errc::mount_sys;
        }

        std::uint64_t dev_flags = MS_NOSUID | MS_STRICTATIME;
        if (::mount("tmpfs", new_dev_.c_str(), "tmpfs", dev_flags, "mode=755") != 0) {
            return mount_errc::mount_dev;
        }

        return mount_errc::ok;
    }

    mount_errc change_root() const noexcept {
        auto new_root = new_root_.c_str();
        if (::mount(new_root, new_root, "bind", MS_BIND | MS_REC, "") != 0) {
            return mount_errc::mount_as_bind;
        }

        constexpr auto perm = 0777;
        auto old_root = old_root_.c_str();
        if (::mkdir(old_root, perm) != 0) {
            return mount_errc::mkdir_old_root_for_pivot;
        }

        // Mount to `new_root` and old root will be attached to `pivot_dir`.
        if (::syscall(SYS_pivot_root, new_root, old_root) != 0) {
            return mount_errc::syscall_pivot_root;
        }

        if (::chdir("/") != 0) {
            return mount_errc::chdir_call;
        }

        constexpr auto old_pivot_root = old_root_after_pivot(k_old_root_name);
        if (::umount2(old_pivot_root.data(), MNT_DETACH) != 0) {
            return mount_errc::unmount_old_pivot;
        }

        if (::rmdir(old_pivot_root.data()) != 0) {
            return mount_errc::rmdir_old_pivot;
        }

        return mount_errc::ok;
    }

private:
    inline static constexpr char k_old_root_name[] = ".old_root";
    std::string new_root_;
    std::string old_root_;
    std::string new_proc_;
    std::string new_sys_;
    std::string new_dev_;
    esl::unique_fd err_pipe_rd_;
    esl::unique_fd err_pipe_wr_;
};

std::filesystem::path get_image_path(std::string_view image_name) {
    std::filesystem::path path("/var/lib/lumper/images");
    path /= image_name;
    return path;
}

} // namespace

void process(cli::cmd_run_t) {
    const auto& parser = cli::for_current_process().command_parser();

    auto argv = parser.present<std::vector<std::string>>("CMD");
    if (!argv || argv->empty()) {
        throw cli_parse_failure("No CMD provided", &parser);
    }

    base::subprocess::options opts;
    opts.clone_with_flags(CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET | CLONE_NEWIPC);
    if (!parser.get<bool>("--it")) {
        opts.set_stdin(base::subprocess::use_null);
        opts.set_stdout(base::subprocess::use_null);
        opts.set_stderr(base::subprocess::use_null);
    }

    auto image_path = get_image_path(parser.get<std::string>("--image"));
    if (!std::filesystem::exists(image_path)) {
        throw std::invalid_argument(
                fmt::format("image path ({}) doesn't exist", image_path.string()));
    }

    mount_proc_before_exec mount_proc(image_path);
    opts.set_evil_pre_exec_callback(&mount_proc);

    cgroups::resource_config res_cfg;

    auto mem_limit = parser.present<std::string>("--memory");
    if (mem_limit) {
        res_cfg.set_memory_limit(*mem_limit);
    }

    auto cpus_limit = parser.present<int>("--cpus");
    if (cpus_limit) {
        res_cfg.set_cpus(*cpus_limit);
    }

    SPDLOG_INFO("Prepare to run cmd: {}", *argv);
    try {
        cgroups::cgroup_manager cgroup_mgr("lumper-cgroup", res_cfg);

        base::subprocess proc(*argv, opts);
        ESL_ON_SCOPE_EXIT {
            base::ignore_unused(proc.wait());
            // NOLINTNEXTLINE(bugprone-lambda-function-name)
            SPDLOG_INFO("Command {} completed", esl::strings::join(*argv, " "));
        };

        cgroup_mgr.apply(proc.pid());
    } catch (const base::spawn_subprocess_error& ex) {
        auto errc = mount_proc.read_error();
        if (errc != mount_errc::ok) {
            SPDLOG_ERROR("Failed to run mount_proc_before_exec; reason={}", mount_errc_msg(errc));
        }
        throw command_run_error(ex.what());
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Failed to run cmd in sub-process; cmd={}", *argv);
        throw command_run_error(ex.what());
    }
}

} // namespace lumper
