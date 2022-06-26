//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include "lumper/mount_container_before_exec.h"

#include <array>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "spdlog/spdlog.h"

namespace lumper {
namespace {

template<std::size_t N>
constexpr auto old_root_after_pivot(const char (&pivot_root)[N]) -> std::array<char, N + 1> {
    std::array<char, N + 1> str{'/'};
    for (std::size_t i = 0; i < N - 1; ++i) {
        str[i + 1] = pivot_root[i];
    }
    return str;
}

int create_directories(std::string_view path) {
    constexpr int buf_size = 4096;
    char buf[buf_size] = {};
    std::memcpy(buf, path.data(), path.size());

    for (std::size_t i = 0; i < path.size();) {
        while (i < path.size() && buf[i] != '/') {
            ++i;
        }

        if (i == 0) {
            ++i;
            continue;
        }

        if (i < path.size()) {
            buf[i] = '\0';
        }

        constexpr mode_t perm = 0777;
        if (auto rc = ::mkdir(buf, perm); rc != 0 && errno != EEXIST) {
            return errno;
        }

        if (i < path.size()) {
            buf[i++] = '/';
        }
    }

    return 0;
}

struct device_info {
    const char* name;
    mode_t type;
    std::uint32_t major_id;
    std::uint32_t minor_id;
};

} // namespace

mount_container_before_exec::mount_container_before_exec(std::string hostname,
                                                         const std::filesystem::path& new_root,
                                                         std::string mount_data)
    : hostname_(std::move(hostname)),
      new_root_(new_root),
      old_root_(new_root / k_old_root_name),
      new_proc_(new_root / "proc"),
      new_sys_(new_root / "sys"),
      new_dev_(new_root / "dev"),
      new_dev_pts_(new_root / "dev" / "pts"),
      mount_data_(std::move(mount_data)) {
    if (mount_data_.empty()) {
        throw std::invalid_argument("empty mount_data");
    }

    int fds[2]{};
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        throw std::system_error(errno,
                                std::system_category(),
                                "failed to pipe2() for mount_proc_before_exec");
    }

    err_pipe_rd_ = esl::wrap_unique_fd(fds[0]);
    err_pipe_wr_ = esl::wrap_unique_fd(fds[1]);
}

// No dynamic allocation is allowed in this function and functions it calls.
int mount_container_before_exec::run() noexcept {
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

mount_errc mount_container_before_exec::read_error() {
    err_pipe_wr_.reset();
    auto errc = mount_errc::ok;
    ssize_t rc = 0;
    do {
        rc = ::read(err_pipe_rd_.get(), &errc, sizeof(errc));
    } while (rc == -1 && errno == EINTR);
    return errc;
}

void mount_container_before_exec::set_volume_dir(volume_pair volume_dir) {
    volume_dir_.emplace(std::move(volume_dir));
    SPDLOG_INFO("Specified data volume: host={} contaienr={}",
                volume_dir_->first, volume_dir_->second);
}

mount_errc mount_container_before_exec::make_contained() const noexcept {
    if (::sethostname(hostname_.data(), hostname_.size()) != 0) {
        return mount_errc::set_hostname;
    }

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

mount_errc mount_container_before_exec::setup_container_root() const noexcept {
    if (::mount("overlay", new_root_.c_str(), "overlay", MS_NODEV, mount_data_.c_str()) != 0) {
        return mount_errc::mount_container_root;
    }

    return mount_errc::ok;
}

mount_errc mount_container_before_exec::create_mounts() const noexcept {
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

    {
        if (create_directories(new_dev_pts_) != 0) {
            return mount_errc::mkdir_dev_pts;
        }

        if (::mount("devpts", new_dev_pts_.c_str(), "devpts", 0, "") != 0) {
            return mount_errc::mount_dev_pts;
        }
    }

    if (auto errc = make_devices(); errc != mount_errc::ok) {
        return errc;
    }

    if (volume_dir_.has_value()) {
        const auto& [in_host, in_container] = *volume_dir_;
        if (create_directories(in_container) != 0) {
            return mount_errc::mkdir_container_volume;
        }

        if (::mount(in_host.c_str(), in_container.c_str(), "bind", MS_BIND | MS_REC, "") != 0) {
            return mount_errc::mount_volume;
        }
    }

    return mount_errc::ok;
}

mount_errc mount_container_before_exec::make_devices() const noexcept {
    constexpr auto dev_path_buf_size = 4096;
    char dev_path_buf[4096] = {};
    std::memcpy(dev_path_buf, new_dev_.data(), new_dev_.size());
    auto dev_path_prefix_len = new_dev_.size();
    if (new_dev_.back() != '/') {
        dev_path_buf[dev_path_prefix_len] = '/';
        ++dev_path_prefix_len;
    }
    auto const dev_path_data_ptr = &dev_path_buf[dev_path_prefix_len];

    // Make standard I/O fds.

    char self_fd_path[] = "/proc/self/fd/x";
    constexpr auto full_len = std::size(self_fd_path) - 1;
    const char* stdios[] = {"stdin", "stdout", "stderr"};
    for (std::size_t i = 0; i < std::size(stdios); ++i) {
        self_fd_path[full_len - 1] = static_cast<char>(static_cast<unsigned char>('0') + i);
        std::strcpy(dev_path_data_ptr, stdios[i]);
        if (::symlink(self_fd_path, dev_path_buf) != 0) {
            return mount_errc::symlink_call;
        }
    }

    self_fd_path[full_len - 2] = '\0';
    std::strcpy(dev_path_data_ptr, "fd");
    if (::symlink(self_fd_path, dev_path_buf) != 0) {
        return mount_errc::symlink_call;
    }

    // Make extra special devices.

    device_info special_devices[] = {
            device_info{"null", S_IFCHR, 1, 3},
            device_info{"zero", S_IFCHR, 1, 5},
            device_info{"random", S_IFCHR, 1, 8},
            device_info{"urandom", S_IFCHR, 1, 9},
            device_info{"console", S_IFCHR, 136, 1}, // NOLINT(readability-magic-numbers)
            device_info{"tty", S_IFCHR, 5, 0},
            device_info{"full", S_IFCHR, 1, 7}};

    for (const auto& dev : special_devices) {
        std::strcpy(dev_path_data_ptr, dev.name);
        auto dev_num = ::makedev(dev.major_id, dev.minor_id);
        constexpr mode_t perm = 0666;
        if (::mknod(dev_path_buf, dev.type | perm, dev_num) != 0) {
            return mount_errc::mknod_call;
        }
    }

    return mount_errc::ok;
}

mount_errc mount_container_before_exec::change_root() const noexcept {
    constexpr auto perm = 0777;
    auto old_root = old_root_.c_str();
    if (::mkdir(old_root, perm) != 0) {
        return mount_errc::mkdir_old_root_for_pivot;
    }

    // Mount to `new_root` and old root will be attached to `pivot_dir`.
    auto new_root = new_root_.c_str();
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

} // namespace lumper
