//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#ifndef LUMPER_MOUNT_CONTAINER_BEFORE_EXEC_H_
#define LUMPER_MOUNT_CONTAINER_BEFORE_EXEC_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "esl/unique_handle.h"

#include "base/subprocess.h"

namespace lumper {

enum class mount_errc : std::uint32_t {
    ok = 0,
    mount_private,
    mount_proc,
    mount_sys,
    mount_dev,
    mount_volume,
    mount_container_root,
    mkdir_container_volume,
    mkdir_old_root_for_pivot,
    syscall_pivot_root,
    chdir_call,
    unmount_old_pivot,
    rmdir_old_pivot,
    set_hostname,
    total_count
};

inline const char* mount_errc_msg(mount_errc errc) noexcept {
    constexpr const char* errc_msgs[] = {"success",
                                         "failed to mount for private namespace",
                                         "failed to mount /proc as proc",
                                         "failed to mount /sys as sysfs",
                                         "failed to mount /dev as tmpfs",
                                         "failed to mount volume",
                                         "failed to mount container root",
                                         "failed to mkdir container volume",
                                         "failed to mkdir old root for pivot",
                                         "failed to call syscall pivot_root",
                                         "failed to chdir to new root",
                                         "failed to unmount old root",
                                         "failed to set container hostname",
                                         "failed to rmdir old root"};
    static_assert(std::size(errc_msgs) == std::size_t(mount_errc::total_count));
    auto idx = static_cast<std::underlying_type_t<mount_errc>>(errc);
    return errc_msgs[idx];
}

class mount_container_before_exec : public base::subprocess::evil_pre_exec_callback {
public:
    using volume_pair = std::pair<std::string, std::string>;

    mount_container_before_exec(std::string hostname,
                                const std::filesystem::path& new_root,
                                std::string mount_data);

    int run() noexcept override;

    mount_errc read_error();

    void set_volume_dir(volume_pair volume_dir);

private:
    mount_errc make_contained() const noexcept;

    mount_errc setup_container_root() const noexcept;

    mount_errc create_mounts() const noexcept;

    mount_errc change_root() const noexcept;

private:
    inline static constexpr char k_old_root_name[] = ".old_root";
    std::string hostname_;
    std::string new_root_;
    std::string old_root_;
    std::string new_proc_;
    std::string new_sys_;
    std::string new_dev_;
    std::string mount_data_;
    std::optional<volume_pair> volume_dir_;
    esl::unique_fd err_pipe_rd_;
    esl::unique_fd err_pipe_wr_;
};

} // namespace lumper

#endif // LUMPER_MOUNT_CONTAINER_BEFORE_EXEC_H_
