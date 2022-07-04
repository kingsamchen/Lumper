//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include "lumper/cli.h"

#include <stdexcept>
#include <string_view>

#include "esl/strings.h"
#include "fmt/printf.h"
#include "fmt/ranges.h"

namespace lumper {
namespace {

constexpr char k_prog_cmd[] = "COMMAND";
constexpr char k_cmd_run[] = "run";
constexpr char k_cmd_ps[] = "ps";
constexpr char k_cmd_rm[] = "rm";

inline void validate(cli::cmd_run_t, const argparse::ArgumentParser* parser) {
    auto argv = parser->present<std::vector<std::string>>("CMD");
    if (!argv || argv->empty()) {
        throw std::invalid_argument("No CMD given!");
    }

    if (parser->get<bool>("--it") && parser->get<bool>("--detach")) {
        throw std::invalid_argument("--it and --detach cannot both be given");
    }
}

inline void validate(cli::cmd_ps_t, const argparse::ArgumentParser* parser) {}

inline void validate(cli::cmd_rm_t, const argparse::ArgumentParser* parser) {}

} // namespace

// static
void cli::init(int argc, const char* argv[]) {
    // Once ctor of `instance` is done, its lifetime will endure until after return from the
    // main function.
    static cli instance;
    current_ = &instance;
    current_->parse(argc, argv);
}

// static
const cli& cli::for_current_process() {
    if (!current_) {
        fmt::print(stderr, "cli is not inited!");
        std::abort();
    }
    return *current_;
}

void cli::parse(int argc, const char* argv[]) {
    argparse::ArgumentParser parser_run("lumper run");
    parser_run.add_argument("--it")
            .help("enable interactive tty")
            .default_value(false)
            .implicit_value(true);
    parser_run.add_argument("-d", "--detach")
            .help("run container in background")
            .default_value(false)
            .implicit_value(true);
    parser_run.add_argument("-i", "--image")
            .help("image name")
            .required();
    parser_run.add_argument("-m", "--memory")
            .help("enable memory limit");
    parser_run.add_argument("--cpus")
            .scan<'i', int>()
            .help("enable cpu limit");
    parser_run.add_argument("-v", "--volume")
            .help("data volume")
            .action([](const std::string& value) {
                auto sp = esl::strings::split(value, ':', esl::strings::skip_empty{});
                if (std::distance(sp.begin(), sp.end()) != 2) {
                    throw std::invalid_argument("invalid volume parameter");
                }
                return value;
            });
    parser_run.add_argument("CMD")
            .help("executable and its arguments (optional)")
            .remaining();
    cmd_parser_table_.emplace(k_cmd_run, cmd_parser{cmd_run_t{}, std::move(parser_run)});

    argparse::ArgumentParser parser_ps("lumper ps");
    parser_ps.add_argument("-a", "--all")
            .help("Show all containers")
            .nargs(0)
            .default_value(false)
            .implicit_value(true);
    cmd_parser_table_.emplace(k_cmd_ps, cmd_parser{cmd_ps_t{}, std::move(parser_ps)});

    argparse::ArgumentParser parser_rm("lumper rm");
    parser_rm.add_argument("container_id")
             .help("container id")
             .required();
    cmd_parser_table_.emplace(k_cmd_rm, cmd_parser{cmd_rm_t{}, std::move(parser_rm)});

    prog_.add_argument(k_prog_cmd)
            .remaining()
            .help("Avaliable commands: \n  " +
                  esl::strings::join(cmd_parser_table_,
                                     "\n  ",
                                     [](const auto& e, std::string& ap) { ap.append(e.first); }));

    std::vector<std::string> prog_args;
    argparse::ArgumentParser* cur_parser{&prog_};
    try {
        prog_.parse_args(argc, argv);
        prog_args = prog_.get<std::vector<std::string>>(k_prog_cmd);
        const auto& cmd = prog_args[0];
        cur_cmd_parser_ = cmd_parser_table_.find(cmd);
        if (cur_cmd_parser_ == cmd_parser_table_.end()) {
            throw std::runtime_error("Unknown command: " + cmd);
        }

        cur_parser = &cur_cmd_parser_->second.parser;
        cur_parser->parse_args(prog_args);

        // Validate for the chosen command.
        std::visit([cur_parser](auto cmd) { validate(cmd, cur_parser); },
                   cur_cmd_parser_->second.cmd);
    } catch (const std::exception& ex) {
        throw cli_parse_failure(ex.what(), cur_parser);
    }
}

} // namespace lumper
