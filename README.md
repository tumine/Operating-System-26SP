# NexOS

NexOS 是一套用于操作系统课程实验的 RISC-V 教学内核。
运行平台为 QEMU `virt` 机器，代码按教学模块拆分（`arch/core/drivers/fs/lib`）。


## 1. 你会用到什么

- 代码仓库：本项目（内核 + 用户态程序 + 构建脚本）
- 运行环境：Linux（推荐 Ubuntu/Debian）
- 模拟器：`qemu-system-riscv64`
- 交叉工具链（满足其一即可）：
  - `riscv64-unknown-elf-*`
  - `riscv64-linux-gnu-*`
- Python：`python3`（用于镜像打包脚本）

## 2. 快速开始（第一次必做）

在仓库根目录执行：

```bash
make
make fsimg
make qemu
```

成功后你会进入系统 shell（提示符通常是 `$ `），可先执行：

```text
help
hello
pid
ls
```

如果你要停止 QEMU，按 `Ctrl+A` 再按 `X`。

## 3. 常用命令

```bash
# 编译内核与编译数据库
make

# 仅重新生成文件系统镜像（会把 user 程序打包进 fs.img）
make fsimg

# 启动系统
make qemu

# GDB 调试
make qemu-gdb
make gdb

# 单独生成 compile_commands.json
make compdb

# 清理构建产物
make clean
```

可选参数示例：

```bash
# 指定 CPU 核数和内存
make qemu CPUS=2 RAM=128M
```

## 4. 最小自测清单（提交前建议）

每次提交实验代码前，至少完成一次最小验证：

```text
hello
pid
forktest
fstest
ls
```

建议记录关键输出到报告中，便于复现实验结果。

## 5. 实验开发工作流（必须遵守）

1. 不要直接在 `main` 上做实验。
2. 每个 Lab 使用独立分支，例如：`lab3-fs`。
3. 小步提交：一次提交只做一件事，提交前确保可编译、可运行最小自测。
4. 提交内容应包含：
   - 代码改动
   - 简短报告（链路图/关键入口/最小验证结果）
   - 必要运行日志

## 6. 目录导读（先看这些）

- `Makefile`：构建入口，先了解目标和依赖。
- `kernel/arch/riscv/`：启动、陷阱向量、上下文切换。
- `kernel/core/`：进程、调度、内存、trap、syscall、文件抽象。
- `kernel/drivers/`：UART、PLIC、VirtIO 磁盘驱动。
- `kernel/fs/`：文件系统实现。
- `kernel/include/`：核心头文件与常量定义。
- `user/`：用户程序与 syscall 封装。
- `tools/`：镜像打包、构建辅助脚本。

## 7. 常见问题（FAQ）

### Q1: `make` 报错找不到 RISC-V 工具链

确认以下命令至少有一套可用：

```bash
riscv64-unknown-elf-gcc --version
# 或
riscv64-linux-gnu-gcc --version
```

若都不可用，请先安装交叉工具链并加入 `PATH`。

### Q2: `make` 报错找不到 `qemu-system-riscv64`

安装 QEMU 后确认：

```bash
qemu-system-riscv64 --version
```

### Q3: 进入 shell 后程序执行失败

先执行 `ls` 检查用户程序是否已打包进镜像；若异常，重新执行：

```bash
make fsimg
make qemu
```

## 8. 说明

- 本仓库用于教学实验，不是生产级操作系统。
- 请优先保证“链路跑通 + 理解路径 + 可复现验证”，再做功能扩展和性能优化。
