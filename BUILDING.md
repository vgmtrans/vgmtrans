# Building VGMTrans

> [!TIP]
> **Building from source is usually not necessary.**
> Precompiled binaries for macOS, Windows, and Linux are available on the [GitHub Releases](https://github.com/vgmtrans/vgmtrans/releases) page and for each commit on [GitHub Actions](https://github.com/vgmtrans/vgmtrans/actions).

This document provides instructions for building VGMTrans from source if you wish to contribute or need a custom build.
We generally accept patches to fix build issues on platforms we don't have easy access to, but we cannot guarantee that we will be able to test them.

## Prerequisites

To build VGMTrans, you need the following tools and libraries:

- **CMake**: Version 3.21 or higher.
- **C++ Compiler**: A compiler supporting C++20 (e.g., Clang 15+, GCC 12+, or MSVC 19.30+, Xcode 16+).
- **Qt 6**: Any 6.x version should work, the latest version is always recommended. Check `src/ui/qt/CMakeLists.txt` for the exact modules required.
- **Ninja**: Required build system used by the project's CMake presets.

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

Choose a preset appropriate for your platform and architecture:

- **macOS (Apple Silicon)**: `macos-arm64`
- **macOS (Intel)**: `macos-intel`
- **Windows (x64)**: `win-x64`
- **Windows (Clang-cl)**: `win-x64-clang`
- **Linux (x64)**: `linux-x64`

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
cmake --build --preset <preset-name>-<config>
```

Example for a release build on Apple Silicon:
```bash
cmake --build --preset macos-arm64-release
```

The compiled binaries will be located in the `build/<preset-name>/<Config>/bin` directory (e.g., `build/macos-arm64/Release/bin`). This is because the project uses multi-config generators to allow quickly switching between Debug and Release builds.

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
- **Qt**: We recommend using the official Qt Online Installer.

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
