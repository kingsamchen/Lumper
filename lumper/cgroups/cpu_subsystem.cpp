//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include "lumper/cgroups/subsystems.h"

#include <cassert>

#include <unistd.h>

#include "esl/scope_guard.h"
#include "fmt/format.h"
#include "fmt/os.h"
#include "spdlog/spdlog.h"

#include "base/file_util.h"
#include "lumper/cgroups/util.h"

namespace lumper::cgroups {
namespace {

constexpr char period_filename[] = "cpu.cfs_period_us";
constexpr char quota_filename[] = "cpu.cfs_quota_us";
constexpr char task_filename[] = "tasks";

} // namespace

cpu_subsystem::cpu_subsystem(std::string_view cgroup_name, int cpus) {
    assert(!cgroup_name.empty());
    assert(cpus > 0);
    cgroup_path_ = get_cgroup_path_for_subsystem(cpu_subsystem::name, cgroup_name, true);
    ESL_ON_SCOPE_FAIL {
        remove();
    };

    auto period_path = cgroup_path_ / period_filename;
    auto period = base::read_file_to_string(period_path);
    auto quota_path = cgroup_path_ / quota_filename;
    base::write_to_file(quota_path, fmt::to_string(cpus * std::stoi(period)));
}

cpu_subsystem::~cpu_subsystem() {
    remove();
}

void cpu_subsystem::apply(int pid) {
    auto task_path = cgroup_path_ / task_filename;
    base::write_to_file(task_path, fmt::to_string(pid));
}

void cpu_subsystem::remove() noexcept {
    auto rc = ::rmdir(cgroup_path_.c_str());
    if (rc != 0 && errno != ENOENT) {
        SPDLOG_ERROR("Failed to cleanup cgroup cpu subsystem; errno={} path={}",
                     errno, cgroup_path_.native());
    }
}

} // namespace lumper::cgroups
