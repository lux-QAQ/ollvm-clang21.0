







# 移植 ollvm(Hikari) 到 llvm+clang21.0
中文 | [English](./README_en.md)


[旧的 ollvm](https://github.com/GreenDamTan/llvm-project_ollvm) 最高到 `llvm+clang17.06` 的，而 llvm+clang17.06 的版本太老了，为了学习`c++`的新特性`import`，所以我决定将 [ollvm(Hikari)](https://github.com/61bcdefg/Hikari-LLVM15) ~~复制~~移植到 llvm+clang21.0 上。    
如果你觉得本项目对你有帮助请点个`star`⭐。


---
## Ollvm
	ollvm是一个基于llvm的代码混淆器，用于反调试和反编译。


## Hikari-LLVM15 项目地址
[Hikari-LLVM15](https://github.com/61bcdefg/Hikari-LLVM15)这个是混淆器的核心实现。 


---
### 使用方法
[HikariObfuscator的wiki](https://github.com/HikariObfuscator/Hikari/wiki/Usage)
这里列出一些常见的使用方法：
```	shell
-mllvm -enable-allobf 全部启用
 -mllvm -enable-bcfobf 启用伪控制流 
 -mllvm -enable-cffobf 启用控制流平坦化
 -mllvm -enable-splitobf 启用基本块分割 
 -mllvm -enable-subobf 启用指令替换 
 -mllvm -enable-acdobf 启用反class-dump 
 -mllvm -enable-indibran 启用基于寄存器的相对跳转，配合其他加固可以彻底破坏IDA/Hopper的伪代码(俗称F5) 
 -mllvm -enable-strcry 启用字符串加密
 -mllvm -enable-funcwra 启用函数封装
```

---
## 安装



### 编译好的版本
[Release](https://github.com/lux-QAQ/ollvm-clang21.0/releases)
你可以下载我编译好的版本然后直接替换到官方的`llvm+clang`中。


---

### 如何自己编译
---
#### Linux 平台（满血版本）
> 注意有个抽象的工具polly没有启用，因为会导致编译错误，除此之外所有项目都编译了。包括`Z3静态分析器`，所以你需要提前使用`apt`安装`z3`
**所需工具**：编译所需的基本东西`build-essential`、`clang18`（我是使用`apt`安装的`clang18`编译成功的，`gcc`或者其他编译器自行研究）
   
找个空间大的地方
```shell
git clone https://github.com/lux-QAQ/ollvm-clang21.0.git -b clang+ollvm-21.0.0 --depth 1 --recursive
```
创建个build文件夹
```shell
mkdir build
cd build
```
执行cmake配置，更改`-DCMAKE_INSTALL_PREFIX=`来改变安装位置
```shell
cmake -G "Ninja"   -DCMAKE_INSTALL_PREFIX=$HOME/llvm   -DLLVM_ENABLE_PROJECTS="bolt;clang;clang-tools-extra;compiler-rt;cross-project-tests;libc;libclc;lld;lldb;mlir;pstl;flang;openmp;bolt"   -DLLVM_ENABLE_RUNTIMES="all"   -DLLVM_ENABLE_Z3_SOLVER=ON   -DLLVM_FORCE_BUILD_RUNTIME=ON      -DCMAKE_C_COMPILER=/home/ljs/llvm/bin/clang   -DCMAKE_CXX_COMPILER=/home/ljs/llvm/bin/clang++   -DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-linux-gnu   -DCMAKE_C_COMPILER_TARGET=x86_64-pc-linux-gnu   -DCMAKE_CXX_FLAGS="-O3 -march=native  "   -DCMAKE_C_FLAGS="-O3 -march=native  "   -DLLVM_PROFILE_GENERATE=OFF   -DLIBCXX_INSTALL_MODULES=ON   -DCMAKE_AR=/home/ljs/llvm/bin/llvm-ar   -DCMAKE_RANLIB=/home/ljs/llvm/bin/llvm-ranlib   -DLLVM_ENABLE_OPENMP=ON   -DLLVM_ENABLE_LIBUNWIND=ON -DBOOTSTRAP_LLVM_ENABLE_LTO="Thin"  -DLLVM_ENABLE_LIBCXXABI=ON     -DCMAKE_BUILD_TYPE=Release   -DLLVM_BUILD_LLVM_DYLIB=ON   -DLLVM_LINK_LLVM_DYLIB=ON   -DLLVM_ENABLE_EXCEPTIONS=ON -DCMAKE_BUILD_WITH_INSTALL_RPATH=TRUE  -DCMAKE_SHARED_LINKER_FLAGS="-Wl"    ../llvm
```


然后启动编译**请你根据你的内存决定编译并发数目**，建议32GB内存以下不要超过12线程
``` shell
ninja -j10
```

等待半个小时如果一切顺利的话。
然后执行安装
``` shell
ninja install
```
---
#### Windows 平台
和这篇文章差不多。详细步骤可参考：
[构建含有ollvm功能的clang-cl](https://www.bilibili.com/opus/943544163969794072)

所需工具`vs studio`+`cmake`
> 我用的编译器是`msvc`。其他的编译器我没有测试过，不过应该也可以编译。   

找个空间大的地方
```shell
git clone https://github.com/lux-QAQ/ollvm-clang21.0.git -b clang+ollvm-21.0.0 --depth 1 --recursive
```
创建个build文件夹
```shell
mkdir build
cd build
```
执行cmake配置
```shell
cmake -DLLVM_ENABLE_PROJECTS="clang;lld;" -DLLVM_INCLUDE_DOCS=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -G "Visual Studio 17 2022" -A x64 -Thost=x64 ..\llvm
```
这里只选择了clang和lld，如果你需要其他的可以自己添加。   
随后进入build文件夹，打开生成的LLVM.sln   
把编译设置为Release，然后编译clang即可。
注意编译的时候会**消耗大量内存（预计会超过25GB）**，如果内存不够请：
1. 减少编译最大并发数
2. 增加虚拟内存

首次编译预计会花费20分钟左右。

编译后在Release/bin目录下会有`clang.exe`和`clang++.exe`，生成。你需要下载官方的`llvm+clang`，然后把你自己编译产物替换到官方的`llvm+clang`的bin中即可。
> 截至2025年2月，官方发布的Release版本是LLVM 20.1.0-rc1。但是我使用的是官方main分支编译的，所以版本号是21.0.0。故这里可能存在一些未知的兼容性问题。


---
## 混淆效果
Ollvm虽然不及很多商业混淆器，但是效果仍然非常强，对付普通的逆向和脚本小子足够了。
### 变量混淆
![1](https://github.com/user-attachments/assets/fe8b2f3a-ac5e-4a2f-b2d8-4cd0790eba25)

### 大量虚假控制流+平坦化
![2](https://github.com/user-attachments/assets/c334d06b-3199-409f-8ba5-7bac49265af3)

### 字符串加密
![3](https://github.com/user-attachments/assets/744419c4-a30f-45de-a467-651c8bf5ee9a)


---
## 已知的问题
### 当前版本 ollvm 已知问题
> 截至2025年2月   
 
1. 在编译复杂代码，使用`-mllvm -enable-allobf`启用所有混淆器时，会消耗`非常多的内存（已请求内存可达100GB+）`，可能会导致电脑崩溃。这个问题可能是由于代码内存泄漏导致的。故在大型项目请勿启用所有混淆器。
2. 虚假控制流参数`-enable-bcfobf`启用时。代码中不能存在（抛出异常好像可以存在）捕获异常的代码，会导致编译器崩溃。这个问题据说是：`ollvm`中`fixstack`函数导致的，如果要修复可能得修改`eh的返回边`还有`invoke的支配关系`。（反正我不是很搞得懂）。**希望有大佬出手。**
3. `ollvm`会影响性能，即使开启`-O3`优化选项性能仍然会下降。而如果开启`-O0`你会发现程序几乎不能正常运行。
4. 开启编译器的优化选项可能会导致`ollvm`混淆器效果下降，所有请根据实际情况选择优化选项。
5. 对于AntiClassDump的功能只适用于Apple平台，其他平台无效。

---

### 关于移植中的一些问题
我发现llvm17.06和21.0的`llvm/lib/Passes/PassBuilderPipelines.cpp`一些代码已经不一样了。根据我粗浅的理解：这里需要确定何种情况下向`MPM`中导入`ObfuscationPass()`。我按照`llvm+clang17`的版本中的情况，将`ObfuscationPass()`导入到了`MPM`中。但是，有一些21.0新增的情况我不知道是否应该添加`ObfuscationPass()`到`MPM`中（我选择的是不添加）。所以这里可能存在一些问题，会导致混淆性能下降。




