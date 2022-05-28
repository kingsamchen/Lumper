
option(LUMPER_ENABLE_CLANG_TIDY "Enable clang-tidy on build" OFF)

if(LUMPER_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE
               NAMES clang-tidy
               DOC "Path to clang-tidy executable")
  if(NOT CLANG_TIDY_EXE)
      message(STATUS "WARNING: clang-tidy Not found.")
  else()
      message(STATUS "LUMPER_ENABLE_CLANG_TIDY = ON: ${CLANG_TIDY_EXE}")
      set(CMAKE_CXX_CLANG_TIDY
          "${CLANG_TIDY_EXE}"
          -header-filter=lumper/)
  endif()
else()
  message(STATUS "LUMPER_ENABLE_CLANG_TIDY = OFF")
endif()
