CPMAddPackage("gh:fmtlib/fmt#8.1.1")
CPMAddPackage("gh:kingsamchen/esl#9a1f02cbea4902ab110ce7e9457f226a2c2c3375")
CPMAddPackage("gh:kingsamchen/uuidxx#6314dbe11f3ce593ac9236129af17cac4766d254")
CPMAddPackage("gh:p-ranav/argparse#v2.2")
CPMAddPackage(
  NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  GIT_TAG v1.10.0
  OPTIONS "SPDLOG_FMT_EXTERNAL ON" "SPDLOG_PREVENT_CHILD_FD ON"
)
CPMAddPackage(
  NAME nlohmann_json
  URL "https://github.com/nlohmann/json/releases/download/v3.10.5/json.tar.xz"
  OPTIONS "JSON_BuildTests OFF" "JSON_MultipleHeaders ON"
)

add_executable(lumper)

target_sources(lumper
  PRIVATE
    cli.cpp
    cli.h
    cgroups/cgroup_manager.cpp
    cgroups/cgroup_manager.h
    cgroups/cpu_subsystem.cpp
    cgroups/memory_subsystem.cpp
    cgroups/subsystems.h
    cgroups/util.cpp
    cgroups/util.h
    command_ps.cpp
    command_run.cpp
    commands.h
    container_info.cpp
    container_info.h
    main.cpp
    mount_container_before_exec.cpp
    mount_container_before_exec.h
    path_constants.h
)

target_include_directories(lumper
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../
)

target_link_libraries(lumper
  PRIVATE
    argparse
    esl
    fmt
    nlohmann_json::nlohmann_json
    spdlog
    uuidxx

    base
)

lumper_apply_common_compile_options(lumper)

if(LUMPER_USE_SANITIZER)
  lumper_apply_sanitizer(lumper)
endif()

target_precompile_headers(lumper
  PRIVATE "${LUMPER_PCH_HEADER}"
)
