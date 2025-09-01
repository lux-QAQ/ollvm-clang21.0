# Porting ollvm (Hikari) to llvm+clang21.0

[中文](./README.md) | English

The existing ollvm [repository](https://github.com/GreenDamTan/llvm-project_ollvm) is based on `llvm+clang17.06`, which is outdated. To learn about the new `import` feature in C++, I decided to port [ollvm(Hikari)](https://github.com/61bcdefg/Hikari-LLVM15) ~~copy~~ port it to llvm+clang21.0.
If you find this project helpful, please give it a `star`⭐.

---
## Ollvm
Ollvm is an LLVM-based code obfuscator used for anti-debugging and anti-decompilation.

## ~~Hikari-LLVM15 Project Link~~ [Deprecated]
The core implementation of the obfuscator is in [Hikari-LLVM15](https://github.com/61bcdefg/Hikari-LLVM15).

**It is expected that the `Ssage` version of OLLVM will be ported over during the July holiday in 2025, adding new features.**
Currently, most of the porting has been completed. The new project after porting, [ollvm-plugins](https://github.com/ollvm-adaplite/ollvm-plugins), may not be merged back into this project and may subsequently serve as a plugin-based obfuscator.

---
### Usage
[HikariObfuscator Wiki](https://github.com/HikariObfuscator/Hikari/wiki/Usage)
Here are some common usage methods:
```	shell
-mllvm -enable-allobf Enable all obfuscations
 -mllvm -enable-bcfobf Enable Bogus Control Flow
 -mllvm -enable-cffobf Enable Control Flow Flattening
 -mllvm -enable-splitobf Enable Basic Block Splitting
 -mllvm -enable-subobf Enable Instruction Substitution
 -mllvm -enable-acdobf Enable Anti-Class Dump
 -mllvm -enable-indibran Enable indirect branching, which, when combined with other hardening, can completely break the pseudocode (aka F5) in IDA/Hopper.
 -mllvm -enable-strcry Enable String Encryption
 -mllvm -enable-funcwra Enable Function Wrapping
```

---
## Installation

### Precompiled Version
[Release](https://github.com/lux-QAQ/ollvm-clang21.0/releases)
You can download my precompiled version and directly replace it in the official `llvm+clang`.

---

### How to Compile It Yourself
---
#### Linux Platform (Full-featured version)
> Note that an abstract tool, polly, is not enabled because it causes compilation errors. All other projects are compiled, including the `Z3 static analyzer`, so you need to install `z3` using `apt` beforehand.
**Required tools**: Basic build tools `build-essential`, `clang18` (I successfully compiled using `clang18` installed via `apt`; for `gcc` or other compilers, you'll have to figure it out yourself).

Find a space with enough storage:
```shell
git clone https://github.com/lux-QAQ/ollvm-clang21.0.git -b clang+ollvm-21.0.0 --depth 1 --recursive
```
Create a build folder:
```shell
mkdir build
cd build
```
Run the CMake configuration, change `-DCMAKE_INSTALL_PREFIX=` to set the installation path:
```shell
cmake -G "Ninja"   -DCMAKE_INSTALL_PREFIX=$HOME/llvm   -DLLVM_ENABLE_PROJECTS="bolt;clang;clang-tools-extra;compiler-rt;cross-project-tests;libc;libclc;lld;lldb;mlir;pstl;flang;openmp;bolt"   -DLLVM_ENABLE_RUNTIMES="all"   -DLLVM_ENABLE_Z3_SOLVER=ON   -DLLVM_FORCE_BUILD_RUNTIME=ON      -DCMAKE_C_COMPILER=/home/ljs/llvm/bin/clang   -DCMAKE_CXX_COMPILER=/home/ljs/llvm/bin/clang++   -DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-linux-gnu   -DCMAKE_C_COMPILER_TARGET=x86_64-pc-linux-gnu   -DCMAKE_CXX_FLAGS="-O3 -march=native  "   -DCMAKE_C_FLAGS="-O3 -march=native  "   -DLLVM_PROFILE_GENERATE=OFF   -DLIBCXX_INSTALL_MODULES=ON   -DCMAKE_AR=/home/ljs/llvm/bin/llvm-ar   -DCMAKE_RANLIB=/home/ljs/llvm/bin/llvm-ranlib   -DLLVM_ENABLE_OPENMP=ON   -DLLVM_ENABLE_LIBUNWIND=ON -DBOOTSTRAP_LLVM_ENABLE_LTO="Thin"  -DLLVM_ENABLE_LIBCXXABI=ON     -DCMAKE_BUILD_TYPE=Release   -DLLVM_BUILD_LLVM_DYLIB=ON   -DLLVM_LINK_LLVM_DYLIB=ON   -DLLVM_ENABLE_EXCEPTIONS=ON -DCMAKE_BUILD_WITH_INSTALL_RPATH=TRUE  -DCMAKE_SHARED_LINKER_FLAGS="-Wl"    ../llvm
```

Then start the build. **Please decide the number of concurrent jobs based on your memory**. It is recommended not to exceed 12 threads for memory under 32GB.
``` shell
ninja -j10
```

Wait for about half an hour if everything goes smoothly.
Then run the installation:
``` shell
ninja install
```
---
#### Windows Platform
The process is similar to this article. Detailed steps can be found in:
[Build Clang-cl with ollvm functionality](https://www.bilibili.com/opus/943544163969794072)

Required tools: `VS Studio` + `CMake`
> I used the `MSVC` compiler. I haven't tested other compilers, but they should work too.

Find a space with enough storage:
``` shell
git clone https://github.com/lux-QAQ/ollvm-clang21.0.git -b clang+ollvm-21.0.0 --depth 1 --recursive
```
Create a build folder:
``` shell
mkdir build
cd build
```
Run the CMake configuration:
``` shell
cmake -DLLVM_ENABLE_PROJECTS="clang;lld;" -DLLVM_INCLUDE_DOCS=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -G "Visual Studio 17 2022" -A x64 -Thost=x64 ..\llvm
```
Here, I selected only `clang` and `lld`. If you need others, you can add them yourself.
Next, go to the build folder, open the generated `LLVM.sln`, set the build configuration to `Release`, and compile `clang`.
**Note**: The build will **consume a lot of memory (expected to exceed 25GB)**. If you run out of memory, please:
1. Reduce the maximum concurrency for building.
2. Increase virtual memory.

The initial build will take approximately 20 minutes.

After the build, you will find `clang.exe` and `clang++.exe` in the `Release/bin` directory. You need to download the official `llvm+clang`, then replace its `bin` folder with your compiled binaries.
> As of February 2025, the official release version is LLVM 20.1.0-rc1. However, I used the official `main` branch for compiling, so the version is 21.0.0. Therefore, there may be some unknown compatibility issues.

---
## Obfuscation Effect
Although Ollvm is not as powerful as many commercial obfuscators, its effect is still very strong, sufficient to deal with ordinary reverse engineering and script kiddies.
### Variable Obfuscation
![1](https://github.com/user-attachments/assets/fe8b2f3a-ac5e-4a2f-b2d8-4cd0790eba25)

### Bogus Control Flow + Flattening
![2](https://github.com/user-attachments/assets/c334d06b-3199-409f-8ba5-7bac49265af3)

### String Encryption
![3](https://github.com/user-attachments/assets/744419c4-a30f-45de-a467-651c8bf5ee9a)

---
## Known Issues
### Known Issues with the Current Version of ollvm
> As of August 2025

1. When compiling complex code with the `-mllvm -enable-allobf` flag to enable all obfuscators, it consumes **a lot of memory (memory requested could exceed 100GB)**, which may cause the computer to crash. This issue may be caused by memory leaks in the code. I have fixed some memory leaks based on `ASAN` reports, but I found that the problem is still not completely resolved. Therefore, do not enable all obfuscators for large projects.
2. When the `-enable-bcfobf` flag for false control flow is enabled, the code cannot contain code that catches exceptions (though throwing exceptions might be allowed). This causes the compiler to crash. This issue is said to be caused by the `fixstack` function in `ollvm`. To fix it, modifications may be required to the "EH return edges" and "invoke dominance relationships" (which I do not fully understand). This issue has been mitigated in the new version, but it is not guaranteed to be **100%** normal.
3. `ollvm` impacts performance. Even with the `-O3` optimization flag, performance still decreases. If `-O0` is enabled, the program may not run properly.
4. Enabling compiler optimization options may reduce the effectiveness of the `ollvm` obfuscator. Therefore, choose optimization options based on your actual situation.
5. The AntiClassDump feature is only applicable to Apple platforms and is ineffective on other platforms.

---

### Issues Encountered During Porting
I found that some code in `llvm/lib/Passes/PassBuilderPipelines.cpp` between llvm17.06 and 21.0 has changed. Based on my basic understanding, it seems that we need to determine when to import `ObfuscationPass()` into `MPM`. I followed the situation from the `llvm+clang17` version and imported `ObfuscationPass()` into `MPM`. However, there are some new situations in 21.0 that I’m not sure whether `ObfuscationPass()` should be added to `MPM` (I chose not to add it). So there may be some issues here that lead to degraded obfuscation
