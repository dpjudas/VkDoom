# libdragonbook
Libdragonbook is a small collection of compiler backend and toolchain technologies.

Its core feature is to offer an Intermediate Representation (IR) builder for compiler frontends. From there it can JIT x64 machine code ready to run for Windows, Linux and macOS.

Libdragonbook is based on the LLVM bitcode, although it does not support generating LLVM bc files at this point. Its C++ API is however very similar to that of LLVM's IRBuilder. The primary purpose of libdragonbook is to offer a lightweight alternative to LLVM for projects where LLVM is too big a dependency.
