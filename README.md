## Welcome to VkDoom!

VkDoom is a source port based on the DOOM engine with a focus on Vulkan and modern computers.

Please see license files for individual contributor licenses.

Visit our [website](https://vkdoom.org) for more details.

### Releases

We do not have any official release of VKDoom yet. You can however download a binary build
of the latest master branch commit at https://github.com/dpjudas/VkDoom/releases/tag/nightly

### Build Guide

For Windows, you need the latest version of Visual Studio, Windows SDK, Git, and CMake to build VkDoom.

For Linux, you need the following:
* libsdl2-dev
* libopenal-dev
* libvpx-dev
* git
* cmake

For Mac, I recommend you use the following project, as it contains all the dependencies and makes building easy:
* https://github.com/ZDoom/zdoom-macos-deps

After you have the packages installed, cd into your projects dir, and do the following to clone:
* git clone https://github.com/dpjudas/VkDoom

Afterwards, cd into it
* cd VkDoom

Make a build folder, then cd into it
* mkdir build
* cd build

For Windows, run the following to prepare your build environment and the second command will build:
* cmake -A x64 ..
* cmake --build . --config Release -- -maxcpucount
(replace x64 with ARM64 if you're building on ARM64)

For Linux, run the following to prepare your build environment and the second command will build:
* cmake .. -DCMAKE_BUILD_TYPE=Release
* make -j $(nproc)

For Mac, if you're using zdoom-macos-deps, simply cd into it and type the following:
* ./build.py --target vkdoom

### Licensed under the GPL v3
##### https://www.gnu.org/licenses/quick-guide-gplv3.en.html
