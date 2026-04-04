# NexOS-AI

NexOS 是一套用于操作系统课程实验的 RISC-V 教学内核，运行在 QEMU `virt` 机器上。
当前进度：第二次实验（`AI`），提供：

- 纯用户态的 LLM 推理程序 `llmrun_smol` / `llmrun_qwen`
- 助教预导出的模型资产打包流程
- 将模型资产随文件系统镜像挂载到系统内的能力

当前分支还没有实现后续文档中提到的内核 AI service、后台 daemon 或 `ai_call` / `ai_submit` 之类接口。
如果你看到 `docs/AIOS_KERNEL_SERVICE_LAB.md` 中的 service 化设计，请把它理解为后续实验路线，不是这个分支已经具备的功能。

## 1. 你会用到什么

- Linux 环境，推荐 Ubuntu / Debian
- `qemu-system-riscv64`
- 一套 RISC-V 交叉工具链，满足其一即可
- `riscv64-unknown-elf-*`
- `riscv64-linux-gnu-*`
- `python3`
- `unzip`

`Makefile` 会自动探测工具链和 QEMU。如果本机没有这些依赖，构建会直接报错。

## 2. 模型准备

这条分支使用的不是原始 HuggingFace 权重，而是助教预先导出的二进制模型资产压缩包。

下载地址：

- SmolLM2-135M: `https://git.ustc.edu.cn/KONC/os-lab/-/raw/main/smol.zip?ref_type=heads&inline=false`
- Qwen3.5-0.8B: `https://git.ustc.edu.cn/KONC/os-lab/-/raw/main/qwen.zip?inline=false`

准备方式：

1. 在仓库根目录创建 `models/`
2. 把压缩包放进去，并且文件名保持为 `smol.zip` 和 `qwen.zip` （`mv`命令）
3. 不需要手动解压，`make qemu-smol`、`make qemu-qwen`、`make qemu-llm` 会自动解压并生成镜像

推荐目录结构：

```text
Spring2026OS/
├── Makefile
├── README.md
├── models/
│   ├── smol.zip
│   └── qwen.zip
└── ...
```

如果只想先检查压缩包是否正确、先完成资产解压，可以分别执行：

```bash
make smol-assets
make qwen-assets
make llm-assets
```

解压后，宿主机会得到：

```text
models/
├── smol.zip
├── qwen.zip
├── SMOL/
│   ├── INFO.TXT
│   ├── CFG.BIN
│   ├── EMB.BIN
│   ├── NRM.BIN
│   ├── ROP.BIN
│   ├── L00.BIN
│   └── ...
└── QWEN/
    ├── INFO.TXT
    ├── CFG.BIN
    ├── EMB.BIN
    ├── NRM.BIN
    ├── ROP.BIN
    ├── LTY.BIN
    ├── L00.BIN
    └── ...
```

说明：

- `smol.zip` 需要能解出 `SMOL/INFO.TXT`
- `qwen.zip` 需要能解出 `QWEN/INFO.TXT`
- Qwen 资产额外依赖 `LTY.BIN`
- 这些文件会被打包进镜像中的 `/AI/SMOL` 和 `/AI/QWEN`

如果你想使用自定义路径，也可以在 `make` 时覆盖变量，例如：

```bash
make qemu-smol PREMODEL_ROOT=/path/to/models
make qemu-smol SMOL_ARCHIVE=/path/to/smol.zip
make qemu-qwen QWEN_ARCHIVE=/path/to/qwen.zip
```

## 3. 如何启动

### 3.1 只跑基础系统

```bash
make qemu
```

这个目标会生成普通的 `fs.img`，里面包含用户程序，但**不包含模型资产**。
它适合做普通 OS 实验，不适合运行 `llmrun_smol` / `llmrun_qwen`。

### 3.2 跑 Smol 模型

```bash
make qemu-smol
```

这个目标会：

1. 编译内核和用户程序
2. 自动解压 `models/smol.zip`
3. 生成带 `/AI/SMOL` 的镜像
4. 以单核 `CPUS=1` 启动 QEMU

### 3.3 跑 Qwen 模型

```bash
make qemu-qwen
```

这个目标会：

1. 编译内核和用户程序
2. 自动解压 `models/qwen.zip`
3. 生成带 `/AI/QWEN` 的镜像
4. 以单核 `CPUS=1` 启动 QEMU

### 3.4 同时打包两个模型

```bash
make qemu-llm
```

这个目标会把 `/AI/SMOL` 和 `/AI/QWEN` 都打进同一个镜像，并以单核 `CPUS=1` 启动 QEMU。镜像更大，构建时间也更长，但适合在同一次启动里同时测试两个模型。

说明：

- `make qemu-smol`、`make qemu-qwen`、`make qemu-llm` 在 `Makefile` 中已经固定为单核启动
- 即使传入 `CPUS=2`、`CPUS=4` 之类参数，这三个目标也仍然只会以 `CPUS=1` 运行

### 3.5 首次进入系统后

成功启动后你会进入系统 shell，提示符通常是 `$ `。可以先执行：

```text
hello
pid
ls
```

所有镜像都会包含：

- `llmrun_smol`
- `llmrun_qwen`

如果你是通过 `qemu-smol`、`qemu-qwen` 或 `qemu-llm` 启动的，那么镜像里还会额外包含：

- `/AI/SMOL`
- `/AI/QWEN`

具体包含哪些目录，取决于你使用的启动目标。

退出 QEMU 的方式：

```text
Ctrl+A, 然后按 X
```

### 3.6 常用构建命令

```bash
make
make fsimg
make fsimg-smol
make fsimg-qwen
make fsimg-llm
make qemu-gdb
make gdb
make compdb
make clean
```

可选参数：

```bash
make qemu CPUS=2 RAM=128M
make qemu-smol RAM=1536M
make qemu-qwen RAM=2048M
```

## 4. 应用如何使用

当前分支里的 LLM 应用是：

- `llmrun_smol`
- `llmrun_qwen`

这两个程序的输入和输出都是 **token id**，不是自然语言文本。
当前分支没有在 guest 内集成 tokenizer / detokenizer，所以：

- 你输入的是一串整数 token
- 程序输出的是生成出的整数 token
- 输出格式是空格分隔的 token id 列表

如果你要做自然语言级别的演示，需要在宿主机侧先把文本编码成 token id，再把输出 token id 反解码回文本。仓库里已经提供了对应工具。

### 4.1 宿主机文本/token 转换工具

工具路径：

- `tools/translate_llm_io.py`

默认 tokenizer 文件：

- `tools/tokenizers/SMOL/tokenizer.json`
- `tools/tokenizers/QWEN/tokenizer.json`

依赖：

- 宿主机需要安装 Python 包 `tokenizers`

安装方式：

```bash
python3 -m pip install --user tokenizers
```

如果你的环境还没有 `pip`，例如在 Ubuntu / Debian 上，可以先安装：

```bash
sudo apt install python3-pip
python3 -m pip install --user tokenizers
```

文本编码为 token id：

```bash
python3 tools/translate_llm_io.py encode --model smol --text "The capital of France is"
python3 tools/translate_llm_io.py encode --model qwen --text "The capital of France is"
```

对应输出：

```text
504 3575 282 4649 314
760 6511 314 9338 369
```

token id 反解码为文本：

```bash
python3 tools/translate_llm_io.py decode --model smol --tokens "504 3575 282 4649 314"
python3 tools/translate_llm_io.py decode --model qwen --tokens "760 6511 314 9338 369"
```

对应输出：

```text
The capital of France is
The capital of France is
```

如果你想覆盖默认 tokenizer 文件，也可以显式指定本地路径：

```bash
python3 tools/translate_llm_io.py encode --model smol --tokenizer-file /path/to/tokenizer.json --text "hello"
```

### 4.2 最简单的运行方式

Smol：

```text
llmrun_smol 1 2 3 4
```

Qwen：

```text
llmrun_qwen 1 2 3 4
```

含义：

- `1 2 3 4` 是输入 prompt 的 token id
- 默认 `--predict 1`
- 程序会输出 1 个新生成的 token id

### 4.3 指定生成 token 数

```text
llmrun_smol --predict 4 1 2 3 4
llmrun_qwen --predict 8 1 2 3 4
```

这会分别生成 4 个或 8 个 token，输出类似：

```text
123 456 789 42
```

这里只是输出格式示例，不代表真实语义。

### 4.4 用 prompt 文件

prompt 文件格式是空白字符分隔的 token id，例如：

```text
1 2 3 4 5 6
```

运行方式：

```text
llmrun_smol --predict 4 --prompt-file prompt.txt
llmrun_qwen --predict 4 --prompt-file prompt.txt
```

限制：

- `--prompt-file` 不能和行内 token 混用
- `--stdin` 不能和 `--prompt-file` 同时使用

### 4.5 交互模式

不带 prompt 参数运行时，程序会进入交互模式。你也可以显式使用 `--stdin`：

```text
llmrun_smol
llmrun_qwen --stdin
```

交互模式中，每一行的格式是：

```text
<predict> <token0> <token1> <token2> ...
```

例如：

```text
4 1 2 3 4
```

含义是：

- 对输入 token `1 2 3 4`
- 继续生成 4 个 token

输入 `exit` 或 `quit` 会返回 shell。

### 4.6 资产目录覆盖

默认资产目录：

- `llmrun_smol` 使用 `/AI/SMOL`
- `llmrun_qwen` 使用 `/AI/QWEN`

如果你把模型打包到了别的位置，可以覆盖：

```text
llmrun_smol --asset-dir /AI/SMOL
llmrun_qwen --asset-dir /AI/QWEN
```

## 5. 使用限制和注意事项

- 单次请求最多接受 255 个输入 token
- 还必须满足 `token_count + predict_count <= runtime_seq_len`
- 如果超过模型资产中的 `runtime_seq_len`，程序会报 `request too long for runtime_seq_len=...`
- token id 不能超出模型词表范围，否则会报 `token i out of range`
- Qwen 资产缺少 `LTY.BIN` 时无法运行
- LLM 相关启动目标 `make qemu-smol`、`make qemu-qwen`、`make qemu-llm` 固定使用 `CPUS=1`
- 这两个程序每次运行都会先加载模型资产，Qwen 会比 Smol 更重

当前分支的重点是“把模型资产装进教学 OS，并在用户态完成推理链路”，不是提供完整的文本聊天体验。

## 6. 常见问题

### Q1: 为什么 `make qemu` 里运行 `llmrun_smol` 失败

因为 `make qemu` 使用的是普通 `fs.img`，不包含 `/AI/SMOL` 或 `/AI/QWEN`。

如果你要跑模型，请使用：

```bash
make qemu-smol
make qemu-qwen
make qemu-llm
```

### Q2: 报错 `Missing teacher-provided ... archive`

说明 `models/smol.zip` 或 `models/qwen.zip` 不存在，或者名字不对。

请确认：

- 文件在仓库根目录下的 `models/`
- 文件名严格是 `smol.zip` / `qwen.zip`

### Q3: 报错 `Archive ... does not contain SMOL/INFO.TXT` 或 `QWEN/INFO.TXT`

说明压缩包内部目录结构不符合 `Makefile` 预期。当前解压逻辑要求压缩包内能找到：

- `SMOL/INFO.TXT`
- `QWEN/INFO.TXT`

### Q4: 为什么输出不是文本

因为 guest 内的 `llmrun_smol` / `llmrun_qwen` 只处理 token id，不直接处理自然语言文本。

如果你要做文本和 token 之间的转换，可以使用宿主机工具：

- `python3 tools/translate_llm_io.py encode --model smol --text "..."`
- `python3 tools/translate_llm_io.py decode --model qwen --tokens "..."`

### Q5: `request too long for runtime_seq_len=...` 是什么意思

说明你的输入 token 数和要生成的 token 数加起来，超过了该模型资产支持的运行时序列长度。

### Q6: 可以参考哪个文档理解后续方向

可以看：

- `docs/AIOS_KERNEL_SERVICE_LAB.md`

但请注意，这份文档描述的是后续 AI service / kernel service 实验主线，不等于 `AI-stu-v3` 当前已经实现的功能。

## 7. 目录导读

- `Makefile`: 构建入口，包含模型资产解压、镜像打包和 QEMU 启动目标
- `kernel/`: 教学内核实现
- `user/llmrun_smol.c`: Smol 用户态推理程序
- `user/llmrun_qwen.c`: Qwen 用户态推理程序
- `user/llmrun_support.c`
- `user/llmrun_support.h`
- `tools/mkfsimg.py`: 文件系统镜像打包脚本
- `tools/translate_llm_io.py`: 宿主机侧文本/token 转换工具
- `tools/tokenizers/`: 宿主机侧 tokenizer.json 文件
- `docs/`: 课程文档与后续实验设计说明

## 8. 最小验证清单

如果你要验证当前分支链路是否跑通，建议至少做一次下面的最小检查：

```text
hello
pid
ls
llmrun_smol --predict 1 1 2 3
llmrun_qwen --predict 1 1 2 3
```

注意：

- `llmrun_smol` 需要在 `make qemu-smol` 或 `make qemu-llm` 启动后验证
- `llmrun_qwen` 需要在 `make qemu-qwen` 或 `make qemu-llm` 启动后验证
