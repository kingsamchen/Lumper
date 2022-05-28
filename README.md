# Lumper

Study Linux containers: rebuild docker from scratch with modern C++.

## Build instructions

Building requires:

- Any modern Linux distro
- C++ 17 compatible compiler
- CMake 3.19 or higher
- Python 3.5 or higher
- Ninja build (optional, will fallback to Makefile if not available)

```shell
$ cd Lumper
$ python3 build.py --sanitizer=false
```

Run `python3 ./build.py --help` for details.

NOTE: If you want fine control over the configuration and building, feel free to use CMake commands directly.
