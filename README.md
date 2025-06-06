## Welcome to VKDoom!

VKDoom is a source port based on the DOOM engine with a focus on Vulkan and modern computers.

Please see license files for individual contributor licenses.

### Releases

We do not have any official release of VKDoom yet. You can however download a binary build of the latest master branch commit at https://github.com/dpjudas/VkDoom/releases/tag/nightly

### Build Guide

## Prep
For Windows, you need the latest version of Visual Studio, Windows SDK, Git, and CMake to build VKDoom.

For Linux, you need the following:
* libsdl2-dev
* libopenal-dev
* libvpx-dev
* git
* cmake

For Mac, the following project is recommended, as it contains all the dependencies and makes building easy: https://github.com/ZDoom/zdoom-macos-deps

## Clone the repo
After you have the packages installed, `cd` into your projects directory, and do the following to clone:

```sh
git clone https://github.com/dpjudas/VkDoom
```

Afterwards, `cd` into it

```sh
cd VkDoom
```

Make a build folder, then `cd` into it

```sh
mkdir build
cd build
```

## Building

### Windows
For Windows, run the following to prepare your build environment and the second command will build:

```sh
cmake -A x64 ..
cmake --build . --config Release -- -maxcpucount
```

(replace x64 with ARM64 if you're building on ARM64)

### Linux - Ninja (Recommended)
For 'ninja', make sure the ninja package is installed on your distro (ex: Debian: `sudo apt install ninja`) - then run the following to prepare your build environment and the second command will build:
```sh
cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake . --build
```

### Linux - Make
For 'make', run the following to prepare your build environment and the second command will build:

```sh
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j $(nproc)
```

### Mac OS
For Mac, if you're using zdoom-macos-deps, simply cd into it and type the following:

```sh
./build.py --target vkdoom
```

### Licensed under the GPL v3
##### https://www.gnu.org/licenses/quick-guide-gplv3.en.html
