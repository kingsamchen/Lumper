//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include "base/file_util.h"

#include <fstream>

namespace base {

void write_to_file(const std::filesystem::path& filepath, std::string_view data) {
    std::ofstream out(filepath);
    if (!out) {
        throw std::filesystem::filesystem_error(
                "cannot open file to write",
                filepath,
                std::error_code(errno, std::system_category()));
    }

    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) {
        throw std::filesystem::filesystem_error(
                "cannot write file",
                filepath,
                std::error_code(errno, std::system_category()));
    }
}

std::string read_file_to_string(const std::filesystem::path& filepath) {
    std::ifstream in;
    in.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    try {
        in.open(filepath);
        std::stringstream data;
        data << in.rdbuf();
        return data.str();
    } catch (const std::ios_base::failure& ex) {
        throw std::filesystem::filesystem_error(
                std::string("cannot read file: ") + ex.what(),
                filepath,
                ex.code());
    }
}

} // namespace base
