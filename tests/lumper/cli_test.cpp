//
// Kingsley Chen <kingsamchen at gmail dot com>
//

#include "doctest/doctest.h"

#include <type_traits>

#include "uuidxx/uuidxx.h"

#include "lumper/cli.h"

#include "tests/stringification.h"

namespace lumper {

class cli_test_stub : public cli {
public:
    using cli::parse;
};

} // namespace lumper

namespace {

using lumper::cli_parse_failure;
using lumper::cli_test_stub;

template<typename T, int N>
int ssize(T (&)[N]) {
    return N;
}

template<typename T>
int ssize(const std::vector<T>& v) {
    return static_cast<int>(v.size());
}

TEST_SUITE_BEGIN("cli");

TEST_CASE("command is mandatory") {
    SUBCASE("throws when command missing") {
        const char* args[] = {"./lumper"};
        cli_test_stub cli;
        CHECK_THROWS_AS(cli.parse(ssize(args), args), cli_parse_failure);
    }

    SUBCASE("throws when unknown command") {
        const char* args[] = {"./lumper", "test"};
        cli_test_stub cli;
        CHECK_THROWS_AS(cli.parse(ssize(args), args), cli_parse_failure);
    }
}

TEST_CASE("command run") {
    std::vector<const char*> args{"./lumper", "run", "-i", "image_name"};
    CAPTURE(args);

    SUBCASE("flag -i is mandatory") {
        args.erase(std::next(args.begin(), 2), args.end());
        REQUIRE_EQ(args.size(), 2);
        args.push_back("some_cmd");
        cli_test_stub cli;
        CHECK_THROWS_AS(cli.parse(ssize(args), args.data()), cli_parse_failure);
    }

    SUBCASE("empty when no cmd provided") {
        cli_test_stub cli;
        CHECK_THROWS_AS(cli.parse(ssize(args), args.data()), cli_parse_failure);
    }

    SUBCASE("run executable only") {
        args.push_back("some_cmd");
        cli_test_stub cli;
        cli.parse(ssize(args), args.data());
        auto cmd = cli.command_parser().get<std::vector<std::string>>("CMD");
        REQUIRE_EQ(cmd.size(), 1);
        CHECK_EQ(cmd[0], "some_cmd");
    }

    SUBCASE("run executable with args") {
        auto exec = {"some_cmd", "arg1", "--flag1", "args2", "--flag2"};
        args.insert(args.end(), exec);
        cli_test_stub cli;
        cli.parse(ssize(args), args.data());
        auto cmd = cli.command_parser().get<std::vector<std::string>>("CMD");
        REQUIRE_EQ(cmd.size(), 5);
        CHECK_EQ(cmd, std::vector<std::string>(exec.begin(), exec.end()));
    }

    SUBCASE("support enable-tty flag") {
        SUBCASE("false when no specified") {
            args.push_back("some_cmd");
            cli_test_stub cli;
            cli.parse(ssize(args), args.data());
            CHECK_FALSE(cli.command_parser().get<bool>("--it"));
        }

        SUBCASE("with being explicitly given") {
            args.push_back("--it");
            args.push_back("some_cmd");
            cli_test_stub cli;
            cli.parse(ssize(args), args.data());
            CHECK(cli.command_parser().get<bool>("--it"));
        }
    }

    SUBCASE("support detach flag") {
        SUBCASE("false when no specified") {
            args.push_back("some_cmd");
            cli_test_stub cli;
            cli.parse(ssize(args), args.data());
            CHECK_FALSE(cli.command_parser().get<bool>("--detach"));
        }

        SUBCASE("with being explicitly given") {
            args.push_back("--detach");
            args.push_back("some_cmd");
            cli_test_stub cli;
            cli.parse(ssize(args), args.data());
            CHECK(cli.command_parser().get<bool>("--detach"));
        }
    }

    SUBCASE("cannot enable both tty and detach flags") {
        args.insert(args.end(), {"--it", "--deatch", "some_cmd"});
        cli_test_stub cli;
        CHECK_THROWS_AS(cli.parse(ssize(args), args.data()), cli_parse_failure);
    }

    SUBCASE("support memory-limit flag") {
        SUBCASE("specify memory limits") {
            args.insert(args.end(), {"-m", "10m", "some_cmd"});
            cli_test_stub cli;
            cli.parse(ssize(args), args.data());
            CHECK_EQ(cli.command_parser().get<std::string>("--memory"), "10m");
        }

        SUBCASE("not present when not specified") {
            args.push_back("some_cmd");
            cli_test_stub cli;
            cli.parse(ssize(args), args.data());
            auto m = cli.command_parser().present("--memory");
            CHECK_FALSE(m.has_value());
        }
    }

    SUBCASE("support cpu-limit flag") {
        SUBCASE("specify cpu limits correctly") {
            args.insert(args.end(), {"--cpus", "2", "some_cmd"});
            cli_test_stub cli;
            cli.parse(ssize(args), args.data());
            CHECK_EQ(cli.command_parser().get<int>("--cpus"), 2);

            SUBCASE("cannot read value as string") {
                CHECK_THROWS_AS(cli.command_parser().get("--cpus"), std::bad_any_cast);
            }
        }

        SUBCASE("not present when not specified") {
            args.push_back("some_cmd");
            cli_test_stub cli;
            cli.parse(ssize(args), args.data());
            auto m = cli.command_parser().present("--cpus");
            CHECK_FALSE(m.has_value());
        }
    }

    SUBCASE("support volume flag") {
        SUBCASE("specify volume folder correctly") {
            args.insert(args.end(), {"-v", "/path/in/host:/path/in/container", "some_cmd"});
            cli_test_stub cli;
            cli.parse(ssize(args), args.data());
            CHECK_EQ(cli.command_parser().get("-v"), "/path/in/host:/path/in/container");
        }

        SUBCASE("incorrect volume param format") {
            args.insert(args.end(), {"-v", "/path/in/container", "some_cmd"});
            cli_test_stub cli;
            CHECK_THROWS_AS(cli.parse(ssize(args), args.data()), cli_parse_failure);
        }
    }
}

TEST_CASE("command ps") {
    std::vector<const char*> args{"./lumper", "ps"};

    SUBCASE("implicit --all argument") {
        cli_test_stub cli;
        cli.parse(ssize(args), args.data());
        CHECK_EQ(cli.command_name(), "ps");
        CHECK_FALSE(cli.command_parser().get<bool>("--all"));
    }

    SUBCASE("exlicitly specify --all") {
        args.push_back("--all");
        cli_test_stub cli;
        cli.parse(ssize(args), args.data());
        CHECK_EQ(cli.command_name(), "ps");
        CHECK(cli.command_parser().get<bool>("--all"));
    }

    SUBCASE("short for -a") {
        args.push_back("-a");
        cli_test_stub cli;
        cli.parse(ssize(args), args.data());
        CHECK_EQ(cli.command_name(), "ps");
        CHECK(cli.command_parser().get<bool>("--all"));
    }
}

TEST_CASE("command rm") {
    std::vector<const char*> args{"./lumper", "rm"};

    SUBCASE("container id is mandatory") {
        cli_test_stub cli;
        CHECK_THROWS_AS(cli.parse(ssize(args), args.data()), cli_parse_failure);
    }

    SUBCASE("get container id") {
        cli_test_stub cli;
        auto fake_container_id = uuidxx::make_v4().to_string();
        args.push_back(fake_container_id.c_str());
        cli.parse(ssize(args), args.data());
        CHECK_EQ(cli.command_name(), "rm");
        CHECK_EQ(cli.command_parser().get("container_id"), fake_container_id);
    }
}

TEST_SUITE_END();

} // namespace
