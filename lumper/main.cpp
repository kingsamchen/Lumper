//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include <cstdlib>
#include <exception>

#include "fmt/ostream.h"
#include "fmt/ranges.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"

#include "lumper/cli.h"
#include "lumper/commands.h"

void initialize_logger() {
    try {
        auto logger = spdlog::basic_logger_mt("main", "/tmp/lumper.log");
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    } catch (const std::exception& ex) {
        fmt::print(stderr, "Cannot initialize logger: {}", ex.what());
        std::exit(1);
    }
}

int main(int argc, const char* argv[]) {
    initialize_logger();

    try {
        lumper::cli::init(argc, argv);
        lumper::cli::for_current_process().process_command([](auto cmd) {
            lumper::process(cmd);
        });
    } catch (const lumper::cli_parse_failure& ex) {
        fmt::print(stderr, "{}\n\n{}", ex.what(), ex.parser());
        return 1;
    } catch (const lumper::command_run_error& ex) {
        fmt::print(stderr, "Unexpected error when running command: {}", ex.what());
        return 1;
    } catch (const std::exception& ex) {
        fmt::print(stderr, "Unexpected error: {}", ex.what());
        return 1;
    }

    return 0;
}
