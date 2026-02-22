# Building VGMTrans

> [!TIP]
> **Building from source is usually not necessary.**
> Precompiled binaries for macOS, Windows, and Linux are available on the [GitHub Releases](https://github.com/vgmtrans/vgmtrans/releases) page and for each commit on [GitHub Actions](https://github.com/vgmtrans/vgmtrans/actions).

This document provides instructions for building VGMTrans from source if you wish to contribute or need a custom build.
We generally accept patches to fix build issues on platforms we don't have easy access to, but we cannot guarantee that we will be able to test them.

## Prerequisites

To build VGMTrans, you need the following tools and libraries:

- **CMake**: Version 3.23 or higher. Use the latest version available.
- **C++ Compiler**: A compiler supporting C++20 (e.g., Clang 15+, GCC 12+, or MSVC 19.30+, Xcode 16+).
  - On Windows, we recommend using clang-cl.
- **Qt 6**: Any 6.x version should work, the latest version is always recommended. Check `src/ui/qt/CMakeLists.txt` for the exact modules required.
- **Ninja**: Required build system used by the project's CMake presets.

> [!WARNING]
> The courtesy Qt6 submodule for Windows is no longer provided. You will need to install Qt6 yourself.
> You can use the official Qt Online Installer, or [aqtinstall](https://github.com/miurahr/aqtinstall) for installing without an account.

### Installing Qt with aqtinstall

If you choose to use `aqtinstall`, you must ensure that you install the required modules and archives. VGMTrans requires `qtshadertools`, as well as the `qtbase` and `qtsvg` archives for UI presentation to compile correctly. 
Please note that `aqtinstall` cannot be used to install Qt versions older than 6.9, as those packages lack the required `qguiprivate` module in the `qtbase` archive.

You can install the necessary components using the following command (adjust the host, architecture and Qt version as needed):

```bash
# Install aqtinstall
pip install aqtinstall

# Example for macOS
aqt install-qt mac desktop 6.8.3 clang_64 -m qtshadertools --archives qtbase qtsvg --outputdir ./my_qt

# Example for Windows MSVC
aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtshadertools --archives qtbase qtsvg --outputdir ./my_qt
```

Make sure to set your `CMAKE_PREFIX_PATH` to the installed Qt directory (e.g., `-DCMAKE_PREFIX_PATH=./my_qt/6.8.3/macos`) when configuring CMake.

## Getting the Source

VGMTrans uses Git submodules for several dependencies. Ensure you clone recursively:

```bash
git clone --recursive https://github.com/vgmtrans/vgmtrans.git
cd vgmtrans
```

If you have already cloned the repository without submodules, run:

```bash
git submodule update --init --recursive
```

## Building with CMake Presets

VGMTrans utilizes [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html) to simplify the configuration and build process.

### 1. Configure

To build with a supported configuration,
choose a preset appropriate for your platform and architecture from thost listed
after running `cmake --list-presets`.

Run the configuration step:
```bash
cmake --preset <preset-name>
```

Example for Apple Silicon:
```bash
cmake --preset macos-arm64
```

### 2. Build

After configuration, build the project using a build preset (usually appending `-debug` or `-release` to the configuration preset name):

```bash
cmake --build --preset <configure-preset-name>-<config>
```

The full list is available after running `cmake --build --list-presets`.

Example for a release build on Apple Silicon:
```bash
cmake --build --preset macos-arm64-release
```

The compiled binaries will be located in the `build/<preset-name>/<Config>/bin` directory (e.g., `build/macos-arm64/Release/bin`). This is because the project prefers multi-config generators to allow quickly switching between Debug and Release builds.

### Side Note: Custom User Presets

If you need to configure custom environment properties for your local machine, you should create a `CMakeUserPresets.json` file at the root of the repository (this file is `.gitignore`d). 
It allows you to inherit from the base project presets while overriding or adding to the CMake environment without modifying `CMakeLists.txt` or `CMakePresets.json`.

For example, to speed up builds on macOS Apple Silicon using `ccache` and establish a custom install directory (`DESTDIR`), you can define this user preset:
```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "macos-arm64-user",
            "inherits": "macos-arm64",
            "displayName": "VGMTrans (Apple Silicon) - User",
            "description": "Build VGMTrans for Apple Silicon Macs with ccache and a custom DESTDIR structure.",
            "environment": {
                "DESTDIR": "${sourceDir}/build/dist/",
                "CMAKE_C_COMPILER_LAUNCHER": "ccache",
                "CMAKE_CXX_COMPILER_LAUNCHER": "ccache"
            }
        }
    ]
}
```
You can then configure the project with `cmake --preset macos-arm64-user`.

## Platform-Specific Notes

### macOS

We recommend using [Homebrew](https://brew.sh/) to install dependencies:

```bash
brew install cmake qt6 ninja
```

As a compiler, both Xcode's Clang and clang from Homebrew should work.

### Windows

- **MSVC**: Ensure you have Visual Studio 2022 with the "Desktop development with C++" workload installed.
- **Clang**: You can use the `win-x64-clang` preset if you have LLVM installed. Note that Clang on Windows requires at least Build Tools for Visual Studio or a `/winsysroot`
- **Qt**: You can use the official Qt Online Installer, or [aqtinstall](https://github.com/miurahr/aqtinstall) for installing without an account.
  Instead of setting `Qt6_DIR`, it is often much easier to set `CMAKE_PREFIX_PATH` to the root of the installed Qt version folder (e.g., `C:\Qt\6.8.0\msvc2022_64`). CMake will then automatically find all the Qt6 modules inside it. You may also need to set your `QT_PLUGIN_PATH` environment variable if you get errors about missing platform plugins.

> [!WARNING]
> MinGW is not tested and may not work.

### Linux

Install the required packages using your distribution's package manager. For Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build qt6-base-dev \
    qt6-shadertools-dev libqt6svg6-dev libxcb-cursor-dev
```

## Optional Components

You can toggle specific application components using CMake options during configuration:

- `ENABLE_UI_QT`: Build the Qt-based GUI (Default: `ON`).
- `ENABLE_CLI`: Build the command-line interface (Default: `ON`).
- `ENABLE_SHELL`: Build the interactive shell (Default: `ON`).

Example of building only the CLI and Shell:
```bash
cmake --preset <preset-name> -DENABLE_UI_QT=OFF
```

## System Dependencies (Advanced)

By default, VGMTrans vendors most of its dependencies (such as `fmt`, `spdlog`, `zlib`, etc.) to ensure consistent, statically-linked builds across platforms. 
If you are a package maintainer or prefer to link against system-provided libraries, you can opt-out of the vendored versions using the following CMake options:

- `USE_SYSTEM_FMT`
- `USE_SYSTEM_SPDLOG`
- `USE_SYSTEM_MIO`
- `USE_SYSTEM_ZLIB`
- `USE_SYSTEM_MINIZIP`
- `USE_SYSTEM_NLOHMANN`
- `USE_SYSTEM_UNARR`
- `USE_SYSTEM_LIBCHDR`

When enabled, CMake will attempt to find the corresponding library using `find_package()`.
It is your responsibility to ensure that the libraries and CMake find modules are available for the configuration you choose to use.

Example, using fmt and zlib from the system:
```bash
cmake --preset <preset-name> -DUSE_SYSTEM_FMT=ON -DUSE_SYSTEM_ZLIB=ON
```
