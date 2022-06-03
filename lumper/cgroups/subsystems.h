//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#pragma once

#ifndef LUMPER_CGROUPS_SUBSYSTEMS_H_
#define LUMPER_CGROUPS_SUBSYSTEMS_H_

#include <filesystem>
#include <string_view>

namespace lumper::cgroups {

class subsystem { // NOLINT(cppcoreguidelines-special-member-functions)
public:
    virtual ~subsystem() = default;

    virtual void apply(int pid) = 0;
};

class memory_subsystem : public subsystem {
public:
    // Caller must guarantee that `cgroup_name` and `memory_limit` both are not empty.
    // Throws
    //  - `std::filesystem::filesystem_error` for filesystem related errors.
    //  - `std::runtime_error` if mount point of `subsystem` cannot be found.
    memory_subsystem(std::string_view cgroup_name, std::string_view memory_limit);

    ~memory_subsystem() override;

    memory_subsystem(const memory_subsystem&) = delete;

    memory_subsystem(memory_subsystem&&) = delete;

    memory_subsystem& operator=(const memory_subsystem&) = delete;

    memory_subsystem& operator=(memory_subsystem&&) = delete;

    // Throws `std::filesystem::filesystem_error` when failed.
    void apply(int pid) override;

private:
    void remove() noexcept;

private:
    std::filesystem::path cgroup_path_;
    static constexpr char name[] = "memory";
};

class cpu_subsystem : public subsystem {
public:
    // Caller must guarantee that `cgroup_name` and `memory_limit` both are not empty.
    // Throws
    //  - `std::filesystem::filesystem_error` for filesystem related errors.
    //  - `std::runtime_error` if mount point of `subsystem` cannot be found.
    cpu_subsystem(std::string_view cgroup_name, int cpus);

    ~cpu_subsystem() override;

    cpu_subsystem(const cpu_subsystem&) = delete;

    cpu_subsystem(cpu_subsystem&&) = delete;

    cpu_subsystem& operator=(const cpu_subsystem&) = delete;

    cpu_subsystem& operator=(cpu_subsystem&&) = delete;

    // Throws `std::filesystem::filesystem_error` when failed.
    void apply(int pid) override;

private:
    void remove() noexcept;

private:
    std::filesystem::path cgroup_path_;
    static constexpr char name[] = "cpu";
};

} // namespace lumper::cgroups

#endif // LUMPER_CGROUPS_SUBSYSTEMS_H_
