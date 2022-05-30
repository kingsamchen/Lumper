//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#pragma once

#ifndef BASE_FILE_UTIL_H_
#define BASE_FILE_UTIL_H_

#include <filesystem>
#include <string_view>

namespace base {

// Throws `std::filesystem::filesystem_error` when failed.
void write_to_file(const std::filesystem::path& filepath, std::string_view data);

// Throws `std::filesystem::filesystem_error` when failed.
std::string read_file_to_string(const std::filesystem::path& filepath);

} // namespace base

#endif // BASE_FILE_UTIL_H_
